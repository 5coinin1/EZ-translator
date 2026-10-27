#include "AppController.h"
#include "app/HoverSelectionGeometry.h"
#include "ui/MainWindow.h"
#include "ui/RegionEditorWindow.h"
#include "ui/region/RegionSnipperOverlay.h"
#include "ui/region/RegionHighlightOverlay.h"
#include "ui/SettingsDialog.h"
#include "ui/MiniFloatBar.h"
#include "ui/TrayIconManager.h"
#include "capture/GdiWindowCapture.h"
#include "capture/WgcWindowCapture.h"
#include "region/RegionManager.h"
#include "detection/ImageHash.h"
#include "ocr/OcrTextAssembler.h"
#include "ocr/OcrWorker.h"
#include "ocr/OcrThreadBudget.h"
#include "overlay/TextOverlay.h"
#include "overlay/OverlayLayout.h"
#include "overlay/TextCover.h"
#include "overlay/ParagraphAnchor.h"
#include "overlay/TextFlow.h"
#include "overlay/TranslationPopup.h"
#include "translation/TranslationWorker.h"
#include "translation/LanguageDetect.h"
#include "translation/DocumentTranslator.h"
#include "core/SettingsStore.h"
#include "platform/GlobalHotkeys.h"
#include "platform/PlatformUtils.h"
#include "ui/FloatingTranslationWindow.h"
#include "ui/RoiPreviewDialog.h"

#include <QFileInfo>

using namespace EZTranslator::OverlayLayout;

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dxgi1_2.h>
#endif

#include <QApplication>
#include <QGuiApplication>
#include <QPainter>
#include <QScreen>
#include <QMessageBox>
#include <QRegularExpression>
#include <QSettings>
#include <QShortcut>
#include <QThread>
#include <QTimer>
#include <QDebug>

#include <algorithm>
#include <climits>
#include <cmath>
#include <memory>

namespace {

struct OcrGpuProbe
{
    bool available{false};
    int adapterIndex{0};
    int vramMb{0};
    QString name;
};

/**
 * DirectML tren iGPU dung RAM he thong nhu VRAM va co the cham hon CPU voi OCR.
 * Chi chon GPU khi DXGI tim thay adapter phan cung co bo nho rieng du dung.
 */
OcrGpuProbe probeDiscreteGpuForOcr()
{
    OcrGpuProbe probe;
#ifdef _WIN32
    IDXGIFactory1* factory = nullptr;
    const HRESULT factoryResult = CreateDXGIFactory1(
        __uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory));
    if (FAILED(factoryResult) || !factory)
        return probe;

    constexpr SIZE_T minimumDedicatedVram = SIZE_T(512) * 1024 * 1024;
    for (UINT index = 0; ; ++index) {
        IDXGIAdapter1* adapter = nullptr;
        const HRESULT enumResult = factory->EnumAdapters1(index, &adapter);
        if (enumResult == DXGI_ERROR_NOT_FOUND)
            break;
        if (FAILED(enumResult) || !adapter)
            continue;

        DXGI_ADAPTER_DESC1 desc = {};
        const bool described = SUCCEEDED(adapter->GetDesc1(&desc));
        adapter->Release();
        if (!described || (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
            continue;

        const QString name = QString::fromWCharArray(desc.Description).trimmed();
        // Intel Arc la GPU roi; cac dong Intel Graphics/UHD/Iris hien tai la iGPU.
        const bool integratedIntel = desc.VendorId == 0x8086
                                     && !name.contains(QStringLiteral("Arc"),
                                                       Qt::CaseInsensitive);
        if (!integratedIntel && desc.DedicatedVideoMemory >= minimumDedicatedVram) {
            probe.available = true;
            probe.adapterIndex = int(index);
            probe.vramMb = int(desc.DedicatedVideoMemory / 1024 / 1024);
            probe.name = name;
            qInfo() << "[OCR] GPU rời được chọn:" << name << "| adapter=" << probe.adapterIndex
                    << "| VRAM MB =" << probe.vramMb;
            break;
        }
    }

    factory->Release();
#endif
    return probe;
}

/** Tên hiển thị ngắn của mã ngôn ngữ (dùng cho status bar). */
QString languageLabel(const QString& code)
{
    if (code == QLatin1String("auto"))
        return QStringLiteral("Tự động");
    if (code == QLatin1String("vi"))
        return QStringLiteral("Tiếng Việt");
    if (code == QLatin1String("en"))
        return QStringLiteral("Tiếng Anh");
    if (code == QLatin1String("ja"))
        return QStringLiteral("Tiếng Nhật");
    if (code == QLatin1String("ko"))
        return QStringLiteral("Tiếng Hàn");
    if (code == QLatin1String("zh"))
        return QStringLiteral("Tiếng Trung");
    if (code == QLatin1String("fr"))
        return QStringLiteral("Tiếng Pháp");
    if (code == QLatin1String("es"))
        return QStringLiteral("Tiếng Tây Ban Nha");
    if (code == QLatin1String("ru"))
        return QStringLiteral("Tiếng Nga");
    if (code == QLatin1String("th"))
        return QStringLiteral("Tiếng Thái");
    return code.toUpper();
}

/**
 * Key ổn định cho bộ nhớ dịch đoạn: bỏ hoa/thường, khoảng trắng và dấu câu để
 * OCR reflow nhẹ khi cuộn không làm mất cache.
 */
QString paragraphCacheKey(const QString& text)
{
    QString key = text.simplified().toCaseFolded();
    static const QRegularExpression punctuation(QStringLiteral("[\\s\\p{P}]+"));
    key.remove(punctuation);
    return key;
}

} // namespace

AppController::AppController(QObject* parent)
    : QObject(parent)
{
    // Nạp TOÀN BỘ cài đặt đã lưu (mọi nhóm, không chỉ nhóm Dịch thuật) qua SettingsStore.
    EZTranslator::SettingsStore::load(m_settings);
    m_clock.start();

    // Phân loại máy một lần: GPU rời/VRAM, RAM, số core. Mọi budget OCR/cache/fps
    // bên dưới lấy từ plan này để máy yếu không bị ép chạy như máy khoẻ.
    const OcrGpuProbe gpu = probeDiscreteGpuForOcr();
    m_gpuAvailable = gpu.available;
    m_gpuAdapterIndex = gpu.adapterIndex;
    bool adapterOverrideOk = false;
    const int adapterOverride =
        qEnvironmentVariableIntValue("EZ_OCR_GPU_ADAPTER", &adapterOverrideOk);
    if (m_gpuAvailable && adapterOverrideOk && adapterOverride >= 0)
        m_gpuAdapterIndex = adapterOverride; // A/B hoặc test fallback
    m_gpuVramMb = gpu.vramMb;
    m_gpuName = gpu.name;
    m_resourcePlan = EZTranslator::detectOcrResourcePlan(m_gpuVramMb);
    m_motionWindowMs = m_resourcePlan.motionWindowMs;

    EZTranslator::OcrScheduler::Options schedule;
    schedule.settleMs = m_resourcePlan.ocrSettleMs;
    schedule.minRunIntervalMs = m_resourcePlan.ocrCooldownMs;
    m_ocrScheduler.setOptions(schedule);

    const char* profileName = m_resourcePlan.profile == EZTranslator::ResourceProfile::Low
                                  ? "low"
                                  : m_resourcePlan.profile == EZTranslator::ResourceProfile::High
                                        ? "high"
                                        : "balanced";
    qInfo().noquote() << "[RESOURCE] profile=" << profileName
                      << "| cores=" << m_resourcePlan.logicalThreads
                      << "| RAM MB=" << m_resourcePlan.totalRamMb
                      << "| GPU=" << (m_gpuAvailable ? m_gpuName : QStringLiteral("none"))
                      << "| VRAM MB=" << m_gpuVramMb
                      << "| settle=" << m_resourcePlan.ocrSettleMs
                      << "| cooldown=" << m_resourcePlan.ocrCooldownMs
                      << "| motion=" << m_motionWindowMs;

    qDebug() << "Creating MainWindow...";
    m_mainWindow = new MainWindow();
    m_mainWindow->setLanguages(m_settings.sourceLanguage, m_settings.targetLanguage);
    // Chế độ hiển thị ban đầu do MainWindow quyết định (card "Cửa sổ nổi" được chọn sẵn).
    const MainWindow::DisplayMode initialMode = m_mainWindow->displayMode();
    m_overlayMode = initialMode != MainWindow::DisplayMode::HoverTranslate;
    m_floatingMode = initialMode == MainWindow::DisplayMode::FloatingWindow;
    qDebug() << "Creating RegionSnipperOverlay...";
    m_snipperOverlay = new RegionSnipperOverlay();
    qDebug() << "Creating RegionHighlightOverlay...";
    m_highlightOverlay = new RegionHighlightOverlay();
    qDebug() << "Creating RegionEditorWindow...";
    m_regionEditor = new RegionEditorWindow();
    qDebug() << "Creating SettingsDialog...";
    m_settingsDialog = new SettingsDialog(m_mainWindow);
    m_settingsDialog->setSettings(m_settings);
    qDebug() << "Creating MiniFloatBar...";
    m_miniFloatBar = new MiniFloatBar();
    qDebug() << "Creating FloatingTranslationWindow...";
    m_floatingWindow = new FloatingTranslationWindow();
    qDebug() << "Creating TrayIconManager...";
    m_trayManager = new TrayIconManager(this);
    qDebug() << "Creating WgcWindowCapture...";
    m_wgcCapture = new EZTranslator::WgcWindowCapture(this);
    qDebug() << "Creating GdiWindowCapture...";
    m_gdiCapture = new EZTranslator::GdiWindowCapture(this);
    m_capture = m_wgcCapture; // mặc định ưu tiên WGC, fallback trong startCapture()
    qDebug() << "Creating TextOverlay...";
    m_textOverlay = new EZTranslator::TextOverlay();
    m_textOverlay->setCoverColor(m_settings.windowColor);
    qDebug() << "Creating TranslationPopup + hover timer...";
    m_hoverPopup = new EZTranslator::TranslationPopup();
    m_hoverTimer = new QTimer(this);
    m_hoverTimer->setInterval(20);
    connect(m_hoverTimer, &QTimer::timeout, this, &AppController::onHoverTick);

    // Timer "ĐUỔI KỊP": pipeline chỉ tiến khi có FRAME mới, mà cửa sổ tĩnh thì WGC không phát frame
    // — sau khi dừng cuộn, settle/cooldown không bao giờ được kiểm tra cho tới lần vẽ lại kế tiếp
    // (con trỏ nháy, thanh cuộn mờ…) ⇒ bản dịch trễ 1-2s. Tick này xử lý lại FRAME CUỐI ở nhịp màn
    // hình (16ms) NHƯNG chỉ khi overlay đang treo (có nội dung chờ dịch) — lúc rảnh không tốn gì.
    m_catchUpTimer = new QTimer(this);
    m_catchUpTimer->setInterval(16);
    connect(m_catchUpTimer, &QTimer::timeout, this, [this]() {
        if (m_state != EZTranslator::TranslationState::Running || m_lastCapture.image.isNull())
            return;
        if (!m_overlaySuspended)
            return; // rảnh (đã hiện bản dịch): không cần đuổi, khỏi tốn CPU
        onCaptureFrame(m_lastCapture);
    });

    // Overlay chỉ được HIỆN khi cửa sổ đích ở tiền cảnh, nhưng pipeline (capture/OCR/dịch) vẫn
    // chạy nền khi người dùng chuyển sang app khác - quay lại là có ngay. Timer này chỉ lo ẩn/hiện
    // lớp vẽ; không có nó thì lúc cửa sổ đích bị che (không có frame mới) overlay sẽ đứng nguyên.
    m_visibilityTimer = new QTimer(this);
    m_visibilityTimer->setInterval(250);
    connect(m_visibilityTimer, &QTimer::timeout, this, &AppController::tickOverlayVisibility);

    // Timer xac minh: khi scroll-tracking giu ban dich cu, van phai OCR lai sau khi anh dung yen
    // de bat false-match/page-jump (neu khong, text cu co the dung mai toi lan doi ke tiep).
    m_verifyTimer = new QTimer(this);
    m_verifyTimer->setInterval(50);
    connect(m_verifyTimer, &QTimer::timeout, this, &AppController::verifyPendingRegions);
    qDebug() << "Setting mock regions...";

    initConnections();

    // Phím tắt TOÀN CỤC: phải tạo sau MainWindow (cần HWND) và sau initConnections để
    // signal 'activated' có chỗ nối. applySettings() ở dưới đăng ký ngay theo m_settings.
    m_hotkeys = new GlobalHotkeys(this);
    connect(m_hotkeys, &GlobalHotkeys::activated, this, &AppController::dispatchHotkey);

    // Áp toàn bộ cài đặt vào runtime (overlay/appearance, fps, phím tắt, autostart, engine...).
    applySettings(/*initial=*/true);

    // Nạp danh sách vùng ban đầu vào dropdown "Chọn vùng dịch" (kể cả khi chưa có vùng
    // nào: dropdown hiện "Toàn bộ cửa sổ" và các nút Sửa/Xoá bị disable).
    m_nextRegionNumber = m_regions.size() + 1;
    refreshRegionList();
}

AppController::~AppController()
{
    const auto stopThread = [](QThread* thread, int timeoutMs) {
        if (!thread)
            return;
        thread->quit();
        thread->wait(timeoutMs);
    };

    stopThread(m_ocrThread, 10000);
    stopThread(m_hoverOcrThread, 10000);
    for (QThread* thread : m_retiredOcrThreads)
        stopThread(thread, 10000);
    m_retiredOcrThreads.clear();

    if (m_translateThread) {
        m_translateThread->quit();
        m_translateThread->wait(1000);
    }
    delete m_mainWindow;
    delete m_regionEditor;
    delete m_snipperOverlay;
    delete m_highlightOverlay;
    delete m_miniFloatBar;
    delete m_floatingWindow;
    delete m_roiPreviewDialog;
    delete m_textOverlay;
    delete m_hoverPopup;
}

void AppController::initConnections()
{
    // MainWindow signals
    connect(m_mainWindow, &MainWindow::requestStartTranslation, this, &AppController::onStartTranslation);
    connect(m_mainWindow, &MainWindow::requestStopTranslation,  this, &AppController::onStopTranslation);
    connect(m_mainWindow, &MainWindow::requestOpenRegionEditor, this, &AppController::onOpenRegionEditor);
    connect(m_mainWindow, &MainWindow::requestClearRegion,      this, [this]() {
        m_regions.clear();
        m_regionScreenRects.clear();
        m_regionManager.clear();
        m_ocrScheduler.reset();
        m_regionBoxes.clear();
        m_regionSourceText.clear();
        m_regionTranslatedText.clear();
    m_paragraphTranslations.clear();
        m_regionLines.clear();
        m_regionStateAtMs.clear();
        m_regionScrollReuseEligible.clear();
        m_regionServedHash.clear();
        m_regionOcrContentHash.clear();
        m_regionServedContentHash.clear();
        m_regionServedProfile.clear();
        m_regionObservedProfile.clear();
        m_regionDisplayProfile.clear();
        m_regionFrameProfile.clear();
        m_regionOverlayTrackers.clear();
        m_regionLastScrollMatchMs.clear();
        m_regionScrolledOverlay.clear();
        m_regionLineTexts.clear();
        m_regionToken.clear();
        m_currentRegionIndex = -1;
        m_editRegionIndex = -1;
        m_nextRegionNumber = 1;
        if (m_textOverlay)
            m_textOverlay->hideOverlay();
        if (m_highlightOverlay) {
            m_highlightOverlay->hideOverlay();
        }
        m_mainWindow->setShowRegionActive(false);
        refreshRegionList();
    });
    connect(m_mainWindow, &MainWindow::requestShowRegion,       this, &AppController::onShowRegion);
    connect(m_mainWindow, &MainWindow::requestPreviewRoi,       this, &AppController::onPreviewRoiRequested);
    connect(m_mainWindow, &MainWindow::requestAddRegion,        this, &AppController::onRequestAddRegion);
    connect(m_mainWindow, &MainWindow::requestEditRegion,       this, &AppController::onRequestEditRegion);
    connect(m_mainWindow, &MainWindow::requestRemoveRegion,     this, &AppController::onRequestRemoveRegion);
    connect(m_mainWindow, &MainWindow::regionSelected,          this, &AppController::onRegionSelected);
    connect(m_mainWindow, &MainWindow::requestOpenRegionList,   this, [this]() {
        // Layout editor: danh sách vùng đầy đủ (thêm/xoá/sửa/kéo) rồi Lưu để áp dụng.
        m_regionEditor->loadRegions(m_regions);
        m_regionEditor->show();
        m_regionEditor->raise();
        m_regionEditor->activateWindow();
    });
    connect(m_mainWindow, &MainWindow::requestOpenSettings,     this, &AppController::onOpenSettings);
    // Section 3: "Cửa sổ nổi" và "Overlay trên màn hình" là HAI chế độ loại trừ nhau.
    // Cửa sổ nổi = popup cạnh con trỏ (hover); Overlay = chữ dịch vẽ đè lên cửa sổ game.
    connect(m_mainWindow, &MainWindow::displayModeChanged, this,
            [this](MainWindow::DisplayMode mode) {
                syncOverlayMode();
                QString label = QStringLiteral("Dịch khi chỉ chuột");
                if (mode == MainWindow::DisplayMode::FloatingWindow)
                    label = QStringLiteral("Cửa sổ nổi");
                else if (mode == MainWindow::DisplayMode::ScreenOverlay)
                    label = QStringLiteral("Overlay trên màn hình");
                m_mainWindow->setStatus(QStringLiteral("Hiển thị: ") + label,
                                        QColor("#38bdf8"));
            });
    // Khi user chon window moi -> bat dau/chuyen capture
    connect(m_mainWindow, &MainWindow::targetWindowSelected,    this, &AppController::onTargetWindowSelected);
    // Doi ngon ngu tren giao dien chinh -> cap nhat engine dich + OCR
    connect(m_mainWindow, &MainWindow::languagesChanged,        this, &AppController::onLanguagesChanged);

    // Capture signals -> AppController (QueuedConnection dam bao cross-thread an toan)
    // Cả 2 backend cùng nối; chỉ backend đang chạy mới phát signal.
    EZTranslator::IWindowCapture* const backends[] = {m_wgcCapture, m_gdiCapture};
    for (EZTranslator::IWindowCapture* capture : backends) {
        connect(capture, &EZTranslator::IWindowCapture::frameReady,
                this, &AppController::onCaptureFrame, Qt::QueuedConnection);
        connect(capture, &EZTranslator::IWindowCapture::captureError,
                this, &AppController::onCaptureError, Qt::QueuedConnection);
    }

    // HighlightOverlay signals
    connect(m_highlightOverlay, &RegionHighlightOverlay::closed, this, [this]() {
        m_mainWindow->setShowRegionActive(false);
    });

    // RegionSnipperOverlay signals
    connect(m_snipperOverlay, &RegionSnipperOverlay::regionSnapped, this, &AppController::onRegionSnapped);
    connect(m_snipperOverlay, &RegionSnipperOverlay::cancelled, this, &AppController::onRegionSnapCancelled);

    // RegionEditorWindow signals
    connect(m_regionEditor, &RegionEditorWindow::saved, this, &AppController::onRegionEditorSaved);
    connect(m_regionEditor, &RegionEditorWindow::cancelled, this, &AppController::onRegionEditorCancelled);

    // MiniFloatBar signals
    connect(m_miniFloatBar, &MiniFloatBar::requestStopTranslation, this, &AppController::onStopTranslation);
    connect(m_miniFloatBar, &MiniFloatBar::requestOpenSettings, this, &AppController::onOpenSettings);

    if (m_floatingWindow) {
        connect(m_floatingWindow, &FloatingTranslationWindow::closedByUser, this,
                &AppController::onStopTranslation);
    }

    // TrayIconManager signals
    connect(m_trayManager, &TrayIconManager::requestShowMainWindow, this, &AppController::onShowMainWindow);
    connect(m_trayManager, &TrayIconManager::requestToggleTranslation, this, &AppController::onToggleTranslation);
    connect(m_trayManager, &TrayIconManager::requestOpenSettings, this, &AppController::onOpenSettings);
    connect(m_trayManager, &TrayIconManager::requestQuit, this, &AppController::onQuit);

    // SettingsDialog signals
    connect(m_settingsDialog, &SettingsDialog::settingsSaved, this, &AppController::onSettingsSaved);
}

bool AppController::startCapture(quintptr windowHandle)
{
    if (windowHandle == 0)
        return false;

    m_captureHandle = windowHandle;
    m_gotFirstFrame = false;
    m_captureFramesSeen = 0;
    m_captureDiagAtMs = -1000000;

    // Xoa trang thai hover cua phien truoc: frame/geometry cu co the khong con
    // dung voi cua so hoac backend moi, gay crop 0x0 o lan start dau.
    m_hoverFrame = QImage();
    m_hoverFrameSize = QSize();
    m_hoverClientPhysical = QRect();
    m_hoverFrameReady = false;
    m_hoverFrameSettle = 0;
    m_hoverEmptyStreak = 0;
    m_hoverProbes.clear();
    m_hoverLineValid = false;
    m_hoverLastValidMs = m_clock.isValid() ? m_clock.elapsed() : 0;
    if (m_hoverPopup)
        m_hoverPopup->hidePopup();

    // Ưu tiên Windows.Graphics.Capture; nếu không start được (OS cũ, helper lỗi,
    // cửa sổ không hỗ trợ) thì fallback sang GDI PrintWindow.
    if (m_wgcCapture && m_wgcCapture->start(windowHandle)) {
        m_capture = m_wgcCapture;
        qDebug() << "[AppController] Capture backend: WGC";

        // WGC chỉ trả frame khi cửa sổ VẼ LẠI; cửa sổ tĩnh sẽ không có frame nào.
        // Không nhận frame trong 1.5s → chuyển sang GDI (PrintWindow chụp được cả khi tĩnh).
        QTimer::singleShot(1500, this, [this]() {
            if (m_gotFirstFrame || m_state != EZTranslator::TranslationState::Running)
                return;
            qWarning() << "[AppController] WGC gave no frame in 1.5s -> GDI fallback";
            m_hoverFrame = QImage();
            m_hoverFrameSize = QSize();
            m_hoverClientPhysical = QRect();
            m_hoverFrameReady = false;
            m_hoverFrameSettle = 0;
            m_hoverProbes.clear();
            m_hoverLineValid = false;
            if (m_hoverPopup)
                m_hoverPopup->hidePopup();
            if (m_wgcCapture)
                m_wgcCapture->stop();
            if (m_gdiCapture && m_gdiCapture->start(m_captureHandle)) {
                m_capture = m_gdiCapture;
                qDebug() << "[AppController] Capture backend: GDI (fallback)";
            }
        });
        return true;
    }

    qWarning() << "[Capture] WGC start failed; trying GDI fallback";
    if (m_gdiCapture && m_gdiCapture->start(windowHandle)) {
        m_capture = m_gdiCapture;
        qDebug() << "[AppController] Capture backend: GDI (fallback)";
        return true;
    }

    qWarning() << "[AppController] No capture backend could start";
    return false;
}

namespace {

/** Thư mục model OCR: ưu tiên biến môi trường EZ_MODELS_DIR, mặc định trong repo. */
/**
 * Anh mau nho de tinh mau chu/nen cua vung. Khong giu full-resolution
 * region image trong m_regionImages: voi nhieu vung/4K, moi anh vai chuc MB.
 */
QImage regionColorSample(const QImage& image)
{
    if (image.isNull())
        return {};
    if (qMax(image.width(), image.height()) <= 1024)
        return image;
    return image.scaled(1024, 1024, Qt::KeepAspectRatio, Qt::FastTransformation);
}

/**
 * UI game/menu: nhieu box ngan (label + value) tren cung man hinh.
 * Web/truyen: it box, moi box la ca dong dai -> giu layout paragraph.
 *
 * EZ_OVERLAY_BOX_LAYOUT=1/0 de force, mac dinh auto.
 */
/**
 * Viet hoa UI: mot so term trong game/menu bi model dich sai nghia hoac qua dai
 * so voi tab hep. Chi ap cho che do UI-box, khong dung cho truyen/web.
 */
/**
 * OCR CJK hay chen space giua tung chu ("记 忆 序 列"), lam model dich sai/bo sot.
 * Bo space khi ca hai ben la CJK; giu space that giua tu Latin.
 */
QString compactCjkSpaces(const QString& input)
{
    const auto isCjk = [](QChar c) {
        const ushort u = c.unicode();
        return (u >= 0x3400 && u <= 0x4DBF) || (u >= 0x4E00 && u <= 0x9FFF)
               || (u >= 0xF900 && u <= 0xFAFF) || (u >= 0x3040 && u <= 0x30FF)
               || (u >= 0xAC00 && u <= 0xD7AF);
    };
    QString out;
    out.reserve(input.size());
    for (int i = 0; i < input.size(); ++i) {
        const QChar c = input.at(i);
        if (c.isSpace() && i > 0 && i + 1 < input.size()
            && isCjk(input.at(i - 1)) && isCjk(input.at(i + 1)))
            continue;
        out.append(c);
    }
    return out;
}

QString normalizeUiText(QString text)
{
    static const struct { const char* from; const char* to; } kReplacements[] = {
        {"Thập Tự Chuẩn Tinh", "tâm ngắm chữ thập"},
        {"Chuẩn Tinh", "tâm ngắm"},
        {"chuẩn tinh", "tâm ngắm"},
        {"记忆序列", "Chuỗi ký ức"},
        {"弦海信标", "Tín hiệu biển khúc"},
        {"活动凭录", "Chứng nhận sự kiện"},
        {"经验加成卡", "Thẻ tăng EXP"},
        {"3胜", "3 thắng"},
        {"已签到", "Đã điểm danh"},
        {"待签到", "Chờ điểm danh"},
        {"活动说明", "Hướng dẫn sự kiện"},
        {"活动中心", "Trung tâm sự kiện"},
        {"赛季限时", "Mùa giải giới hạn"},
        {"晶源感染", "Nhiễm nguồn tinh thể"},
        {"新人专属", "Độc quyền tân thủ"},
        {"聊天", "Trò chuyện"},
        {"返回", "Quay lại"},
        {"弦歌礼赠", "Quà tặng Khúc Ca"},
        {"Hạn dùng bộ tâm ngắm hình chữ thập", "Áp dụng tâm ngắm chữ thập"},
        {"Mở ra", "Bật"},
        {"Vây ngoài", "Viền ngoài"},
        {"Máy ngoài", "Viền ngoài"},
        {"Mấy ngoài", "Viền ngoài"},
        {"Linh Mẫn Độ", "Độ nhạy"},
        {"Thiết bị điều khiển", "Điều khiển"},
        {"Tài khoản hợp quy", "Tài khoản"},
        {"trung tâm tâm ngắm", "giữa tâm ngắm"},
        {"Hạn dùng bộ chuẩn tinh hình chữ thập", "Áp dụng tâm ngắm chữ thập"},
        {"Đợi đến khi điểm danh", "Chờ điểm danh"},
        {"Hoạt động giải thích", "Hướng dẫn"},
        {"Thiết lập", "Cài đặt"},
        {"Đối chiến", "Đấu"},
        {"Cơ sở", "Cơ bản"},
        {"Thao tác", "Tác vụ"},
        {"Độ nhạy", "Nhạy"},
        {"Hình ảnh", "Hình"},
        {"Âm thanh", "Âm"},
    };
    for (const auto& replacement : kReplacements)
        text.replace(QString::fromUtf8(replacement.from), QString::fromUtf8(replacement.to));
    if (!text.isEmpty() && text.at(0).isLower())
        text[0] = text.at(0).toUpper();
    return text;
}

/** Tab bar rat hep: dung nhan ngan thay vi de ellipsis tren moi tab. */
QString normalizeUiTabText(QString text)
{
    static const struct { const char* from; const char* to; } kReplacements[] = {
        {"Thiết bị điều khiển", "Điều"},
        {"Tài khoản hợp quy", "Tài khoản"},
        {"Thiết lập", "Cài đặt"},
        {"Đối chiến", "Đấu"},
        {"Cơ bản", "Cơ"},
        {"Cơ sở", "Cơ"},
        {"Thao tác", "Tác"},
        {"Tác vụ", "Tác"},
        {"Độ nhạy", "Nhạy"},
        {"Nhạy", "Nhạy"},
        {"Tâm ngắm", "Ngắm"},
        {"Chuẩn Tinh", "Ngắm"},
        {"Hình ảnh", "Hình"},
        {"Thanh âm", "Âm"},
        {"Âm thanh", "Âm"},
        {"Điều khiển", "Điều"},
        {"Trung tâm hoạt động", "Sự kiện"},
        {"Trung tâm sự kiện", "Sự kiện"},
        {"Giới hạn thời gian của mùa giải", "Mùa giải"},
        {"Mùa giải giới hạn", "Mùa giải"},
        {"Mập béo", "Cá béo"},
        {"Người mới độc quyền", "Tân thủ"},
        {"Người mới", "Tân thủ"},
        {"Độc quyền tân thủ", "Tân thủ"},
        {"Tính nguyên nhiễm", "Nhiễm"},
        {"Tinh nguyên nhiễm", "Nhiễm"},
        {"Nhiễm nguồn tinh thể", "Nhiễm"},
        {"Những", "Khác"},
        {"Tài khoản", "Tài khoản"},
    };
    for (const auto& replacement : kReplacements)
        text.replace(QString::fromUtf8(replacement.from), QString::fromUtf8(replacement.to));
    text = text.trimmed();
    if (text.endsWith(QStringLiteral("...")))
        text.chop(3);
    return text;
}

bool useUiBoxLayout(const QList<EZTranslator::OcrTextBox>& boxes)
{
    const QString mode = qEnvironmentVariable("EZ_OVERLAY_BOX_LAYOUT", QStringLiteral("auto"))
                             .trimmed().toLower();
    if (mode == QLatin1String("1") || mode == QLatin1String("on")
        || mode == QLatin1String("box"))
        return true;
    if (mode == QLatin1String("0") || mode == QLatin1String("off")
        || mode == QLatin1String("paragraph"))
        return false;

    if (boxes.size() < 6)
        return false;

    int shortCount = 0;
    int total = 0;
    for (const EZTranslator::OcrTextBox& box : boxes) {
        const QString text = box.text.simplified();
        if (text.isEmpty())
            continue;
        ++total;
        if (text.size() <= 40)
            ++shortCount;
    }
    return total > 0 && shortCount * 100 >= total * 70;
}

QString ocrModelsDir()
{
    const QByteArray override = qgetenv("EZ_MODELS_DIR");
    if (!override.isEmpty())
        return QString::fromLocal8Bit(override);
    return QStringLiteral(EZ_DEFAULT_MODELS_DIR);
}

} // namespace

EZTranslator::OcrOptions AppController::ocrOptions(bool hoverWorker) const
{
    const QString dir = ocrModelsDir();
    const bool english = m_settings.sourceLanguage.startsWith(QStringLiteral("en"),
                                                             Qt::CaseInsensitive);
    const bool japanese = m_settings.sourceLanguage.startsWith(QStringLiteral("ja"),
                                                              Qt::CaseInsensitive);
    const QString japanModel = dir + QStringLiteral("/japan_PP-OCRv4_rec_infer.onnx");

    EZTranslator::OcrOptions options;
    options.detModelPath = dir + QStringLiteral("/ch_PP-OCRv4_det_infer.onnx");
    if (english) {
        options.recModelPath = dir + QStringLiteral("/en_PP-OCRv4_rec_mobile.onnx");
        options.dictionaryPath = dir + QStringLiteral("/en_dict.txt");
    } else if (japanese && QFileInfo::exists(japanModel)) {
        // Model chữ Nhật riêng (kanji + kana) — tốt hơn hẳn model Trung cho tiếng Nhật.
        options.recModelPath = japanModel;
        options.dictionaryPath = dir + QStringLiteral("/japan_dict.txt");
    } else {
        options.recModelPath = dir + QStringLiteral("/ch_PP-OCRv4_rec_infer.onnx");
        options.dictionaryPath = dir + QStringLiteral("/ppocr_keys_v1.txt");
    }
    const auto normalizeDevice = [](QString device) {
        device = device.trimmed().toLower();
        if (device != QLatin1String("cpu") && device != QLatin1String("directml")
            && device != QLatin1String("cuda")) {
            device = QStringLiteral("auto");
        }
        return device;
    };
    const QString detDevice = normalizeDevice(m_settings.ocrDetDevice);
    const QString recDevice = normalizeDevice(m_settings.ocrRecDevice);
    const bool detGpu = detDevice != QLatin1String("cpu");
    const bool recGpu = recDevice != QLatin1String("cpu");

    options.detDevice = detDevice;
    options.recDevice = recDevice;
    options.useGpu = detGpu || recGpu; // legacy + worker fallback
    options.gpuAdapterIndex = m_gpuAdapterIndex;
    options.hoverWorker = hoverWorker;
    options.lowResource = m_resourcePlan.profile == EZTranslator::ResourceProfile::Low;
    if (options.useGpu && !m_gpuAvailable) {
        qInfo() << "[OCR] Không phát hiện GPU rời đủ VRAM; OCR sẽ fallback CPU.";
    }

    // Det và Rec có budget riêng: det CPU có thể 1-2 thread trong khi rec GPU
    // chỉ 1 session; ngược lại rec CPU dùng nhiều worker.
    const EZTranslator::OcrThreadBudget cpuBudget =
        EZTranslator::chooseOcrThreadBudget(false, hoverWorker, m_resourcePlan.profile);
    const EZTranslator::OcrThreadBudget gpuBudget =
        EZTranslator::chooseOcrThreadBudget(true, hoverWorker, m_resourcePlan.profile);
    options.threads = detGpu ? gpuBudget.detectorIntraOp : cpuBudget.detectorIntraOp;
    options.recThreads = recGpu ? gpuBudget.recognizerIntraOp : cpuBudget.recognizerIntraOp;
    options.recWorkers = recGpu ? gpuBudget.recognizerWorkers : cpuBudget.recognizerWorkers;
    options.interOpThreads = 1;
    options.binarize = m_settings.ocrBinarize;
    options.lineCacheLimit = m_resourcePlan.lineCacheLimit;

    // Rec head của PP-OCR tăng chi phí rất mạnh theo chiều rộng. Trên CPU, chia
    // dòng dài thành các mảnh chồng lấn 480 px rồi ghép text nhanh hơn 2-3 lần
    // so với đưa nguyên dòng 1000-1900 px vào model. GPU giữ ảnh rộng để giảm
    // số lần dispatch; biến môi trường phục vụ A/B trên máy người dùng.
    options.recMaxWidth = EZTranslator::chooseOcrRecMaxWidth(recGpu, m_gpuVramMb);
    bool recWidthOk = false;
    const int requestedRecWidth =
        qEnvironmentVariableIntValue("EZ_OCR_REC_MAX_WIDTH", &recWidthOk);
    if (recWidthOk && requestedRecWidth >= 128)
        options.recMaxWidth = qBound(128, requestedRecWidth, 1920);

    // Cài đặt chỉ cho chọn engine đã cài đặt thật (paddleocr); nếu ai đó sửa registry
    // thành engine chưa hỗ trợ thì cảnh báo rõ thay vì im lặng chạy nhầm.
    if (m_settings.ocrEngine != QLatin1String("paddleocr")) {
        qWarning() << "[OCR] engine" << m_settings.ocrEngine
                   << "chưa được hỗ trợ — dùng engine PaddleOCR tích hợp";
    }
    return options;
}

void AppController::startOcr()
{
    if (m_ocrReady.load() || m_ocrLoadAttempted)
        return;
    m_ocrLoadAttempted = true;

    qRegisterMetaType<EZTranslator::OcrOptions>("EZTranslator::OcrOptions");
    qRegisterMetaType<QList<EZTranslator::OcrTextBox>>("QList<EZTranslator::OcrTextBox>");

    m_mainWindow->setStatus(QStringLiteral("Đang nạp model OCR..."), QColor("#38bdf8"));

    const EZTranslator::OcrOptions options = ocrOptions(/*hoverWorker=*/false);
    const int generation = ++m_ocrGeneration; // worker cũ (nếu còn) không còn hiệu lực

    // Worker VÙNG luôn cần. Worker HOVER (thread riêng, để probe không xếp hàng sau lượt OCR vùng)
    // chỉ sinh theo NHU CẦU ở ensureHoverOcr() — chế độ Overlay không dùng hover, mà mỗi worker là
    // một bản engine OCR đầy đủ: nạp sẵn là phí ~150MB RAM (người dùng phản ánh app ăn RAM).
    spawnOcrWorker(m_ocrThread, m_ocrWorker, options, generation, /*hoverWorker=*/false);
}

void AppController::ensureHoverOcr()
{
    if (m_hoverOcrWorker || !m_ocrLoadAttempted)
        return;
    m_hoverOcrReady = false;
    spawnOcrWorker(m_hoverOcrThread, m_hoverOcrWorker,
                   ocrOptions(/*hoverWorker=*/true), m_ocrGeneration,
                   /*hoverWorker=*/true);
}

void AppController::spawnOcrWorker(QThread*& thread, EZTranslator::OcrWorker*& worker,
                                   const EZTranslator::OcrOptions& options, int generation,
                                   bool hoverWorker)
{
    thread = new QThread(this);
    worker = new EZTranslator::OcrWorker();
    worker->moveToThread(thread);
    connect(thread, &QThread::started, worker, [worker, options]() { worker->load(options); });
    connect(worker, &EZTranslator::OcrWorker::loaded, this,
            [this, generation, hoverWorker](bool ok, const QString& provider, const QString& error) {
                onOcrLoaded(generation, hoverWorker, ok, provider, error);
            },
            Qt::QueuedConnection);

    if (hoverWorker) {
        connect(worker, &EZTranslator::OcrWorker::hoverProbed,
                this, &AppController::onHoverProbed, Qt::QueuedConnection);
        connect(worker, &EZTranslator::OcrWorker::selectionRecognized,
                this, &AppController::onSelectionRecognized, Qt::QueuedConnection);
    } else {
        connect(worker, &EZTranslator::OcrWorker::regionRecognizedPartial,
                this, &AppController::onRegionRecognizedPartial, Qt::QueuedConnection);
        connect(worker, &EZTranslator::OcrWorker::regionRecognized,
                this, &AppController::onRegionRecognized, Qt::QueuedConnection);
        connect(worker, &EZTranslator::OcrWorker::regionScrollMatched,
                this, &AppController::onRegionScrollMatched, Qt::QueuedConnection);
        connect(worker, &EZTranslator::OcrWorker::regionPlateReady,
                this, &AppController::onRegionPlateReady, Qt::QueuedConnection);
        connect(worker, &EZTranslator::OcrWorker::regionFinished,
                this, &AppController::onRegionOcrFinished, Qt::QueuedConnection);
    }

    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    thread->start();
}

void AppController::stopOcr()
{
    ++m_ocrGeneration; // vô hiệu hoá signal `loaded` của worker cũ
    m_ocrReady.store(false);
    m_ocrLoadAttempted = false;
    m_hoverOcrReady = false;
    m_ocrProvider.clear();
    m_hoverProbeRunning = false;
    m_pendingHoverProbe.valid = false;
    m_hoverProbes.clear();
    m_regionOcrMailbox.clear();
    m_regionVerifyPending.clear();
    if (m_verifyTimer)
        m_verifyTimer->stop();

    const auto stopWorker = [this](QThread*& thread, EZTranslator::OcrWorker*& worker) {
        if (worker)
            worker->disconnect(); // chặn kết quả OCR cũ trước khi thread dừng
        if (thread) {
            thread->quit();
            if (thread->wait(10000)) {
                thread->deleteLater();
            } else {
                // ONNX có thể đang chạy 1 lượt dài; không xoá thread đang chạy.
                qWarning() << "[OCR] luồng OCR chưa dừng sau 10s; sẽ chờ lúc thoát app";
                QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
                m_retiredOcrThreads.append(thread);
            }
            thread = nullptr;
        }
        // worker được xoá qua QThread::finished → deleteLater
        worker = nullptr;
    };

    stopWorker(m_ocrThread, m_ocrWorker);
    stopWorker(m_hoverOcrThread, m_hoverOcrWorker);
}

void AppController::reloadOcr()
{
    stopOcr();

    m_regionOcrInfo.clear();
    m_regionOcrToken.clear();
    m_regionOcrMailbox.clear();
    m_regionLinePitch.clear();
    m_regionParagraphRatio.clear();
    m_regionBoxes.clear();
    m_regionSourceText.clear();
    m_regionTranslatedText.clear();
    m_paragraphTranslations.clear();
    m_regionLines.clear();
    m_regionStateAtMs.clear();
    m_regionScrollReuseEligible.clear();
    m_regionServedHash.clear();
    m_regionOcrContentHash.clear();
    m_regionServedContentHash.clear();
    m_regionServedProfile.clear();
    m_regionObservedProfile.clear();
    m_regionDisplayProfile.clear();
    m_regionFrameProfile.clear();
    m_regionOverlayTrackers.clear();
    m_regionLastScrollMatchMs.clear();
    m_regionScrolledOverlay.clear();
    m_regionToken.clear();
    m_ocrScheduler.reset();

    m_hoverProbes.clear();
    m_hoverPrefetch = HoverPrefetch();
    m_hoverLineValid = false;
    m_hoverPending = false;
    if (m_hoverPopup)
        m_hoverPopup->hidePopup();
    if (m_textOverlay)
        m_textOverlay->hideOverlay();

    // Đang dịch thì nạp lại ngay; đang Idle thì để lần bấm Bắt đầu nạp theo ngôn ngữ mới.
    if (m_state == EZTranslator::TranslationState::Running)
        startOcr();
}

void AppController::onOcrLoaded(int generation, bool hoverWorker, bool ok, const QString& provider,
                                const QString& error)
{
    if (generation != m_ocrGeneration)
        return; // worker của lần cấu hình cũ

    if (hoverWorker) {
        // Worker hover (chỉ sinh ở chế độ "Cửa sổ nổi"): không ảnh hưởng trạng thái worker vùng.
        m_hoverOcrReady = ok;
        if (ok)
            qInfo() << "[OCR] worker hover sẵn sàng";
        else
            qWarning() << "[OCR] worker hover nạp thất bại:" << error;
        return;
    }

    if (!ok) {
        qWarning() << "[OCR] worker vùng nạp thất bại:" << error;
        m_mainWindow->setStatus(QStringLiteral("Không nạp được OCR (xem ez_debug.log)"),
                                QColor("#ef4444"));
        return;
    }
    if (m_ocrReady.load())
        return;

    m_ocrProvider = provider;
    const bool anyCpu = m_ocrProvider.contains(QStringLiteral("CPU"));
    const bool anyGpu = m_ocrProvider.contains(QStringLiteral("DirectML"))
                        || m_ocrProvider.contains(QStringLiteral("CUDA"));
    if (anyCpu) {
        const QString detail = error.left(90);
        m_mainWindow->setStatus(QStringLiteral("OCR: ") + m_ocrProvider
                                    + (detail.isEmpty() ? QString() : QStringLiteral(" (")
                                                                     + detail + ')'),
                                anyGpu ? QColor("#f59e0b") : QColor("#38bdf8"));
        if (!detail.isEmpty())
            qInfo() << "[OCR] fallback detail:" << detail;
    } else {
        m_mainWindow->setStatus(QStringLiteral("OCR: ") + m_ocrProvider, QColor("#22c55e"));
    }

    qInfo() << "[OCR] ready (worker vùng), provider =" << m_ocrProvider;
    m_ocrReady.store(true);
    // Chế độ Overlay không dùng hover — chỉ bật popup khi đang ở "Cửa sổ nổi".
    if (m_state == EZTranslator::TranslationState::Running && !m_overlayMode)
        startHover();
}

EZTranslator::TranslationConfig AppController::translationConfig(const QString& source) const
{
    EZTranslator::TranslationConfig config;
    // Auto-detect may finish before the translation worker exists. In that case the
    // first configure() must receive the detected language, not the literal "auto";
    // otherwise local pair-specific models (e.g. CT2 en->vi) are never selected.
    if (!source.isEmpty()) {
        config.source = source;
    } else if (EZTranslator::LanguageDetect::isAuto(m_settings.sourceLanguage)
               && !m_detectedSource.isEmpty()) {
        config.source = m_detectedSource;
    } else {
        config.source = m_settings.sourceLanguage;
    }
    config.target = m_settings.targetLanguage.isEmpty() ? QStringLiteral("vi")
                                                        : m_settings.targetLanguage;
    config.engine = m_settings.translationEngine;
    config.googleApiKey = m_settings.googleApiKey;
    config.aiEndpoint = m_settings.aiEndpoint;
    config.aiApiKey = m_settings.aiApiKey;
    config.aiModel = m_settings.aiModel;
    config.aiSystemPrompt = m_settings.aiSystemPrompt;
    config.cacheCapacity = qMin(m_settings.cacheCapacityLines, m_resourcePlan.translationCacheLines);
    config.device = m_settings.translateDevice.trimmed().toLower();
    if (config.device != QLatin1String("cpu") && config.device != QLatin1String("cuda"))
        config.device = QStringLiteral("auto");
    return config;
}

bool AppController::ensureTranslator()
{
    if (m_translateWorker)
        return true;

    qRegisterMetaType<EZTranslator::TranslatedDocument>("EZTranslator::TranslatedDocument");
    qRegisterMetaType<EZTranslator::TranslationConfig>("EZTranslator::TranslationConfig");

    m_translateThread = new QThread(this);
    m_translateWorker = new EZTranslator::TranslationWorker();
    m_translateWorker->moveToThread(m_translateThread);

    m_translationConfig = translationConfig();
    const EZTranslator::TranslationConfig config = m_translationConfig;
    connect(m_translateThread, &QThread::started, m_translateWorker,
            [this, config]() { m_translateWorker->configure(config); });
    connect(m_translateWorker, &EZTranslator::TranslationWorker::translated,
            this, &AppController::onTranslationReady, Qt::QueuedConnection);
    connect(m_translateThread, &QThread::finished, m_translateWorker, &QObject::deleteLater);

    m_translateThread->start();
    qInfo() << "[Translate] worker started, from" << config.source << "to" << config.target
            << "| engine =" << config.engine;
    return true;
}

QRect AppController::targetClientRectPhysical() const
{
    QRect rect;
#ifdef _WIN32
    const quintptr handle = m_mainWindow->selectedWindowHandle();
    if (handle != 0) {
        HWND targetHwnd = reinterpret_cast<HWND>(handle);
        if (IsWindow(targetHwnd)) {
            RECT clientRc = {};
            POINT origin = {0, 0};
            if (GetClientRect(targetHwnd, &clientRc) && ClientToScreen(targetHwnd, &origin)) {
                const int physW = clientRc.right - clientRc.left;
                const int physH = clientRc.bottom - clientRc.top;
                if (physW > 0 && physH > 0)
                    rect = QRect(origin.x, origin.y, physW, physH);
            }
        }
    }
#endif
    return rect;
}

QRect AppController::targetClientRect() const
{
    const QRect physical = targetClientRectPhysical();
    if (physical.isEmpty())
        return {};

    qreal dpr = 1.0;
    if (QScreen* screen = QGuiApplication::primaryScreen())
        dpr = screen->devicePixelRatio();
    if (dpr <= 0.0)
        dpr = 1.0;

    return QRect(qRound(physical.x() / dpr), qRound(physical.y() / dpr),
                 qRound(physical.width() / dpr), qRound(physical.height() / dpr));
}

namespace {

/** Đổi toạ độ con trỏ (pixel thật) sang logical của Qt để đặt popup. */
QPoint cursorToLogical(const QPoint& physical)
{
    qreal dpr = 1.0;
    if (QScreen* screen = QGuiApplication::primaryScreen())
        dpr = screen->devicePixelRatio();
    if (dpr <= 0.0)
        dpr = 1.0;
    return QPoint(qRound(physical.x() / dpr), qRound(physical.y() / dpr));
}

/** Đổi một kích thước (pixel thật) sang logical của Qt. */
int sizeToLogical(int physical)
{
    qreal dpr = 1.0;
    if (QScreen* screen = QGuiApplication::primaryScreen())
        dpr = screen->devicePixelRatio();
    if (dpr <= 0.0)
        dpr = 1.0;
    return qRound(physical / dpr);
}

const QString kHoverId = QStringLiteral("hover");

/**
 * Dải dò theo từng bước: {bề rộng, chiều cao}.
 *
 *  - bước 0 (1200×96): chỉ dùng cho probe DÒ TRƯỚC lúc con trỏ đang di chuyển — nhanh,
 *    shape cố định, không cần đủ đoạn.
 *  - bước 1 (1200×260): probe "con trỏ đã dừng" — cao đủ ~6-8 dòng để THUẬT TOÁN tự
 *    gom đoạn (groupParagraphs) và tự cắt ở khe; không phụ thuộc việc khối có chạm mép
 *    dải hay không (chính chỗ đó làm trước đây kẹt ở 1 dòng).
 *  - các bước sau: nới thêm khi đoạn vẫn còn chạm mép dải / câu còn dài hơn dải.
 */
constexpr struct { int width; int height; } kBandStages[] = {
    {1200, 96}, {1200, 260}, {1600, 300}, {2200, 340}, {3000, 380},
};
constexpr int kBandStageCount = int(sizeof(kBandStages) / sizeof(kBandStages[0]));

/// Tong RAM toi da cho plate (anh nen da xoa chu) cua MOI vung. Plate la anh
/// full-vung nen voi layout nhieu vung/4K co the rat lon.
constexpr qint64 kPlateCacheBudgetBytes = 48LL * 1024 * 1024;

/// Gioi han so dong hover giu lai de tra cuu (tranh tang mai khi luot web dai).
constexpr int kHoverCacheLimit = 1000;

} // namespace

void AppController::startHover()
{
    if (!m_hoverTimer)
        return;
    // Worker OCR hover sinh theo NHU CẦU (chỉ ở chế độ "Cửa sổ nổi") — xem ensureHoverOcr().
    ensureHoverOcr();
    m_hoverLineValid = false;
    m_hoverPending = false;
    m_hoverTranslation.clear();
    m_hoverLineHeight = 0;
    m_hoverLastCursor = QPoint(-100000, -100000);
    m_hoverProbedCursor = QPoint(-100000, -100000);
    m_hoverSentCursor = QPoint(-100000, -100000);
    m_hoverLastSendMs = -1000000;
    m_hoverPrefetch = HoverPrefetch();
    m_hoverProbes.clear();
    m_hoverLastFinalToken = 0;
    m_hoverStillSince = m_clock.isValid() ? m_clock.elapsed() : 0;
    m_hoverEmptyStreak = 0;
    m_hoverLastValidMs = m_clock.isValid() ? m_clock.elapsed() : 0;
    m_selectionButtonDown = false;
    m_selectionActive = false;
    m_selectionFrame = QImage();
    m_selectionScreenRect = QRect();
    m_hoverTimer->start();
    qInfo() << "[Hover] enabled";
}

void AppController::stopHover()
{
    if (m_hoverTimer)
        m_hoverTimer->stop();
    if (m_hoverPopup)
        m_hoverPopup->hidePopup();
    m_hoverLineValid = false;
    m_hoverPending = false;
    m_hoverTranslation.clear();
    m_hoverPrefetch = HoverPrefetch();
    m_hoverProbes.clear();
    m_hoverFrame = QImage();
    m_hoverFrameSize = QSize();
    m_hoverClientPhysical = QRect();
    m_hoverFrameReady = false;
    m_hoverFrameSettle = 0;
    m_hoverEmptyStreak = 0;
    m_selectionButtonDown = false;
    m_selectionActive = false;
    m_selectionFrame = QImage();
    m_selectionScreenRect = QRect();
    ++m_selectionToken;
    if (m_hoverOcrWorker)
        m_hoverOcrWorker->noteSelectionRequest(m_selectionToken);
    ++m_hoverTranslationToken; // vô hiệu hoá kết quả dịch đang về
}

void AppController::syncOverlayMode()
{
    const MainWindow::DisplayMode mode = m_mainWindow->displayMode();
    const bool regionMode = mode != MainWindow::DisplayMode::HoverTranslate;
    const bool floating = mode == MainWindow::DisplayMode::FloatingWindow;
    if (regionMode == m_overlayMode && floating == m_floatingMode)
        return;
    m_overlayMode = regionMode;
    m_floatingMode = floating;

    if (!m_overlayMode) {
        // Hover dùng pipeline riêng. Hủy các ROI đang chờ và đánh dấu request đang
        // chạy là stale để worker dừng sau inference hiện tại, không làm thêm OCR ROI.
        m_regionOcrMailbox.clearPending();
        for (auto it = m_regionOcrToken.begin(); it != m_regionOcrToken.end(); ++it) {
            it.value() = 0;
            if (m_ocrWorker)
                m_ocrWorker->noteRegionRequest(it.key(), 0);
            m_regionOcrMailbox.remove(it.key());
        }
        m_regionVerifyPending.clear();
        m_forceVerifyFrame = false;
        m_ocrScheduler.reset();
        qInfo() << "[OCR_MODE] region OCR paused; display mode is hover";
    }

    if (m_floatingMode) {
        // Cua so noi: dung pipeline vung/OCR giong overlay nhung khong ve
        // lop phu trong suot len cua so dich.
        stopHover();
        if (m_hoverPopup)
            m_hoverPopup->hidePopup();
        if (m_textOverlay)
            m_textOverlay->hideOverlay();
        if (m_state == EZTranslator::TranslationState::Running && m_floatingWindow) {
            m_floatingWindow->show();
            m_floatingWindow->raise();
            refreshFloatingWindow();
        }
    } else if (m_overlayMode) {
        // Sang overlay: tat popup hover va cua so noi.
        stopHover();
        if (m_hoverPopup)
            m_hoverPopup->hidePopup();
        if (m_floatingWindow)
            m_floatingWindow->hide();
        updateOverlay(m_lastFrameSize);
    } else {
        // Sang dich khi chi chuot: tat overlay va cua so noi.
        if (m_textOverlay)
            m_textOverlay->hideOverlay();
        if (m_floatingWindow)
            m_floatingWindow->hide();
        if (m_state == EZTranslator::TranslationState::Running && m_ocrReady.load())
            startHover();
    }
}

void AppController::onHoverTick()
{
    if (!m_hoverPopup)
        return;

    // Đọc lại chế độ từ UI MỖI tick: đang Overlay thì hover PHẢI im (hai chế độ độc lập). Không
    // phụ thuộc cờ m_overlayMode cũ — nó có thể lệch nếu signal đổi chế độ bị lỡ.
    const MainWindow::DisplayMode hoverMode = m_mainWindow->displayMode();
    m_overlayMode = hoverMode != MainWindow::DisplayMode::HoverTranslate;
    m_floatingMode = hoverMode == MainWindow::DisplayMode::FloatingWindow;

    // Chẩn đoán: 2 giây/lần ghi LÝ DO tick không dò được gì. Không có log này thì "hover
    // không dịch" không phân biệt được là do cửa sổ/tiền cảnh/frame hay do OCR/dịch.
    const qint64 diagNow = m_clock.isValid() ? m_clock.elapsed() : 0;
    const bool diag = diagNow - m_hoverDiagMs >= 2000;
    if (diag)
        m_hoverDiagMs = diagNow;
    const auto note = [&](const QString& reason) {
        if (diag) {
            qDebug().noquote() << "[Hover] tick:" << reason << "| frame" << m_hoverFrameSize
                               << "| cho" << m_hoverProbes.size() << "| pending"
                               << m_hoverPending;
        }
    };

    const bool active = m_state == EZTranslator::TranslationState::Running
                        && m_ocrReady.load() && m_hoverFrameReady
                        && m_hoverFrameSize.isValid() && !m_overlayMode;
    if (!active) {
        note(QStringLiteral("chưa đủ điều kiện (chưa bật dịch / OCR chưa xong / chưa có frame)"));
        m_hoverPopup->hidePopup();
        m_hoverLineValid = false;
        m_hoverPending = false;
        if (m_selectionActive || m_selectionButtonDown) {
            ++m_selectionToken;
            if (m_hoverOcrWorker)
                m_hoverOcrWorker->noteSelectionRequest(m_selectionToken);
            ++m_hoverTranslationToken;
        }
        m_selectionButtonDown = false;
        m_selectionActive = false;
        m_selectionFrame = QImage();
        return;
    }

#ifdef _WIN32
    const quintptr handle = m_mainWindow->selectedWindowHandle();
    if (handle == 0 || GetForegroundWindow() != reinterpret_cast<HWND>(handle)) {
        note(QStringLiteral("cửa sổ đích KHÔNG ở tiền cảnh"));
        m_hoverPopup->hidePopup();
        m_hoverLineValid = false;
        m_hoverPending = false;
        if (m_selectionActive || m_selectionButtonDown) {
            ++m_selectionToken;
            if (m_hoverOcrWorker)
                m_hoverOcrWorker->noteSelectionRequest(m_selectionToken);
            ++m_hoverTranslationToken;
        }
        m_selectionButtonDown = false;
        m_selectionActive = false;
        m_selectionFrame = QImage();
        return;
    }

    POINT cursorNative = {};
    if (!GetCursorPos(&cursorNative))
        return;
    const QPoint screenPoint(cursorNative.x, cursorNative.y);

    // Popup bôi chọn có thanh cuộn: tương tác trong popup không phải nét bôi mới.
    if (m_selectionActive && m_hoverPopup->isVisible()
        && m_hoverPopup->geometry().contains(cursorToLogical(screenPoint)))
        return;

    const QRect client = m_hoverClientPhysical;
    const bool leftDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    if (leftDown && !m_selectionButtonDown) {
        m_selectionButtonDown = true;
        m_selectionActive = false;
        m_selectionFrame = client.contains(screenPoint) ? m_hoverFrame : QImage();
        m_selectionFrameClient = client;
        m_selectionDragStart = screenPoint;
        m_selectionScreenRect = QRect();
        m_hoverPopup->hidePopup();
        m_hoverLineValid = false;
        m_hoverPending = false;
        m_pendingHoverProbe.valid = false;
        m_hoverPrefetch = HoverPrefetch();
        m_hoverProbes.clear();
        m_hoverLastFinalToken = 0;
        ++m_selectionToken;
        if (m_hoverOcrWorker)
            m_hoverOcrWorker->noteSelectionRequest(m_selectionToken);
        ++m_hoverTranslationToken;
    } else if (!leftDown && m_selectionButtonDown) {
        m_selectionButtonDown = false;
        sendHoverSelection(screenPoint);
        m_selectionFrame = QImage();
    }
    if (leftDown || m_selectionActive)
        return;

    if (client.isEmpty() || !client.contains(screenPoint)) {
        note(QStringLiteral("con trỏ NGOÀI vùng client"));
        m_hoverPopup->hidePopup();
        m_hoverLineValid = false;
        m_hoverPending = false;
        return;
    }

    const qint64 now = m_clock.isValid() ? m_clock.elapsed() : 0;

    const bool moved = (screenPoint - m_hoverLastCursor).manhattanLength() > 3;
    if (moved) {
        m_hoverLastCursor = screenPoint;
        m_hoverStillSince = now;

        // DÒ TRƯỚC trong lúc con trỏ còn di chuyển: khi con trỏ dừng thì chữ đã đọc
        // sẵn, chỉ còn phải dịch — đây là mấu chốt tốc độ của bản gốc.
        // Chỉ dò trước khi worker OCR còn RẢNH. Đơn vị giờ là cả đoạn (~130ms/lượt) nên
        // gửi dày hơn thế làm hàng đợi phình vô hạn ⇒ hover "đơ" và mọi kết quả về sau
        // đều bị coi là cũ. Đây chính là lỗi làm nó chạy được một lúc rồi đứng.
        if (m_hoverProbes.size() < 3 && now - m_hoverLastSendMs >= 100
            && (screenPoint - m_hoverSentCursor).manhattanLength() >= 6) {
            note(QStringLiteral("đang di chuyển → gửi DÒ TRƯỚC"));
            sendHoverProbe(screenPoint, /*prefetch=*/true);
        }
        return;
    }

    if (now - m_hoverStillSince < 35)
        return;

    const QPoint clientPoint = screenPoint - client.topLeft();

    // Dòng đang hiển thị/đợi dịch vẫn chứa con trỏ, hoặc vừa dò đúng vị trí này →
    // chỉ dò lại thưa (bắt nội dung đổi dưới con trỏ).
    const bool onShownLine = m_hoverLineValid
                             && (m_hoverPending || !m_hoverTranslation.isEmpty())
                             && m_hoverLineClient.adjusted(-2, -4, 2, 4).contains(clientPoint);
    if ((onShownLine || screenPoint == m_hoverProbedCursor)
        && now - m_hoverLastProbeMs < 350) {
        return;
    }

    // Có kết quả DÒ TRƯỚC phủ đúng con trỏ → dùng luôn, bỏ được ~45ms OCR.
    if (m_hoverPrefetch.atMs >= 0 && now - m_hoverPrefetch.atMs <= 400
        && m_hoverPrefetch.lineClient.adjusted(-2, -4, 2, 4).contains(clientPoint)
        && m_hoverPrefetch.text != m_hoverSource) {
        note(QStringLiteral("dùng kết quả dò trước"));
        m_hoverLastProbeMs = now;
        m_hoverProbedCursor = screenPoint;
        showHoverLine(m_hoverPrefetch.text, m_hoverPrefetch.lineClient, screenPoint);
        return;
    }

    note(QStringLiteral("con trỏ đã dừng → gửi probe CHÍNH THỨC"));
    // Gioi han tan suat probe ca khi khong tim thay dong: truoc day nhanh
    // "khong co text" gui probe moi 20ms -> CPU va popup/cursor nhap nhay.
    if (now - m_hoverLastSendMs < 120)
        return;

    sendHoverProbe(screenPoint, /*prefetch=*/false);
#endif
}

void AppController::sendHoverProbe(const QPoint& screenPoint, bool prefetch, int stage)
{
    // Worker hover chỉ sinh khi vào chế độ "Cửa sổ nổi"; trong 1-2s đầu nó còn nạp model thì
    // bỏ probe (không gửi vào engine chưa nạp xong).
    if (!m_hoverOcrWorker || !m_hoverOcrReady)
        return;
    if (!m_hoverFrameReady || m_hoverFrameSize.isEmpty()
        || m_hoverClientPhysical.isEmpty())
        return;

    // Không xếp thêm QMetaCallEvent khi worker đang OCR. Chỉ giữ request mới nhất;
    // probe chính thức luôn thay probe prefetch đang chờ.
    if (m_hoverProbeRunning) {
        if (!m_pendingHoverProbe.valid || !prefetch || m_pendingHoverProbe.prefetch) {
            m_pendingHoverProbe.screenPoint = screenPoint;
            m_pendingHoverProbe.prefetch = prefetch;
            m_pendingHoverProbe.stage = stage;
            m_pendingHoverProbe.valid = true;
        }
        return;
    }
#ifdef _WIN32
    const QRect client = m_hoverClientPhysical;
    const QPoint clientPoint = screenPoint - client.topLeft();

    // Map con trỏ (client pixel thật) → toạ độ frame (ảnh capture).
    const double scaleX = client.width() > 0
                              ? double(m_hoverFrameSize.width()) / double(client.width()) : 1.0;
    const double scaleY = client.height() > 0
                              ? double(m_hoverFrameSize.height()) / double(client.height()) : 1.0;
    const int fx = int(std::lround(clientPoint.x() * scaleX));
    const int fy = int(std::lround(clientPoint.y() * scaleY));

    const int frameW = m_hoverFrameSize.width();
    const int frameH = m_hoverFrameSize.height();

    // Giữ band cố định làm baseline an toàn. Bật A/B bằng EZ_HOVER_ADAPTIVE_BAND=1 để
    // mở rộng khi detector báo dòng chạm mép, tránh làm thay đổi mặc định khi chưa đo accuracy.
    const bool adaptiveBand = qEnvironmentVariable("EZ_HOVER_ADAPTIVE_BAND", QStringLiteral("0"))
                                   .trimmed()
                               != QStringLiteral("0");
    const int useStage = adaptiveBand ? qBound(0, stage, kBandStageCount - 1) : 0;
    const int wantedW = kBandStages[useStage].width;
    const int wantedH = kBandStages[useStage].height;
    const int bandW = qMin(qMax(240, frameW), wantedW);
    const int bandH = qMin(qMax(48, frameH), wantedH);
    const int bandX = qBound(0, fx - bandW / 2, qMax(0, frameW - bandW));
    const QRect srcRect(bandX, fy - bandH / 2, bandW, bandH);

    qInfo().noquote() << "[HOVER_BAND] adaptive=" << adaptiveBand << "stage=" << useStage
                      << "rect=" << srcRect;

    QImage bandImage(bandW, bandH, QImage::Format_RGB32);
    bandImage.fill(QColor(200, 200, 200));
    const QRect inFrame = srcRect.intersected(QRect(QPoint(0, 0), m_hoverFrameSize));
    if (!inFrame.isEmpty()) {
        QPainter bandPainter(&bandImage);
        bandPainter.drawImage(inFrame.topLeft() - srcRect.topLeft(), m_hoverFrame, inFrame);
    }

    const quint64 token = ++m_hoverToken;
    HoverProbeGeom geom;
    geom.bandOrigin = srcRect.topLeft();
    geom.scaleX = scaleX;
    geom.scaleY = scaleY;
    geom.cursorScreen = screenPoint;
    geom.prefetch = prefetch;
    geom.stage = useStage;
    geom.canGrow = (bandW < frameW) || (bandH < frameH);
    m_hoverProbes.insert(token, geom);
    // Dọn các probe không bao giờ trả về (worker bỏ request cũ) để map không phình.
    while (m_hoverProbes.size() > 8)
        m_hoverProbes.erase(m_hoverProbes.begin());
    if (!prefetch)
        m_hoverLastFinalToken = token;

    m_hoverProbeRunning = true;

    m_hoverLastSendMs = m_clock.isValid() ? m_clock.elapsed() : 0;
    m_hoverSentCursor = screenPoint;

    const QPoint cursorLocal(fx - srcRect.x(), fy - srcRect.y());
    const qint64 queuedAtMs = EZTranslator::ocrMonotonicMs();
    const bool queued = QMetaObject::invokeMethod(
        m_hoverOcrWorker, "probeHover", Qt::QueuedConnection,
        Q_ARG(QString, QString::number(token)), Q_ARG(QImage, bandImage),
        Q_ARG(QPoint, cursorLocal), Q_ARG(qint64, queuedAtMs));
    if (!queued) {
        m_hoverProbeRunning = false;
        m_hoverProbes.remove(token);
        flushPendingHoverProbe();
    }
#else
    Q_UNUSED(screenPoint);
    Q_UNUSED(prefetch);
    Q_UNUSED(stage);
#endif
}

void AppController::flushPendingHoverProbe()
{
    if (m_hoverProbeRunning || !m_pendingHoverProbe.valid)
        return;

    const PendingHoverProbe pending = m_pendingHoverProbe;
    m_pendingHoverProbe.valid = false;
    sendHoverProbe(pending.screenPoint, pending.prefetch, pending.stage);
}

QRect AppController::hoverSelectionLogicalRect() const
{
    return QRect(cursorToLogical(m_selectionScreenRect.topLeft()),
                 QSize(sizeToLogical(m_selectionScreenRect.width()),
                       sizeToLogical(m_selectionScreenRect.height())));
}

void AppController::sendHoverSelection(const QPoint& releaseScreen)
{
    if (m_selectionFrame.isNull() || !m_hoverOcrWorker || !m_hoverOcrReady
        || m_selectionFrameClient.isEmpty())
        return;

    const QRect selected = EZTranslator::hoverSelectionEndpointRect(
                               m_selectionDragStart, releaseScreen)
                               .intersected(m_selectionFrameClient);
    // Click thông thường tiếp tục dùng hover; chỉ kéo đủ xa mới coi là chọn chữ.
    if (selected.width() < 8
        || (releaseScreen - m_selectionDragStart).manhattanLength() < 12)
        return;

    const QRect readRect = EZTranslator::hoverSelectionReadRect(
        m_selectionDragStart, releaseScreen, m_selectionFrameClient);
    const double scaleX = double(m_selectionFrame.width()) / m_selectionFrameClient.width();
    const double scaleY = double(m_selectionFrame.height()) / m_selectionFrameClient.height();
    const QRect frameRect(
        int(std::floor((readRect.left() - m_selectionFrameClient.left()) * scaleX)),
        int(std::floor((readRect.top() - m_selectionFrameClient.top()) * scaleY)),
        int(std::ceil(readRect.width() * scaleX)),
        int(std::ceil(readRect.height() * scaleY)));
    const QRect cropped = frameRect.intersected(m_selectionFrame.rect());
    if (cropped.isEmpty())
        return;

    QImage image = m_selectionFrame.copy(cropped);
    constexpr qint64 maxPixels = 3000000;
    if (qint64(image.width()) * image.height() > maxPixels) {
        const double factor = std::sqrt(double(maxPixels)
                                        / double(qint64(image.width()) * image.height()));
        image = image.scaled(qMax(1, qRound(image.width() * factor)),
                             qMax(1, qRound(image.height() * factor)),
                             Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }

    m_selectionActive = true;
    m_selectionScreenRect = readRect;
    const quint64 token = ++m_selectionToken;
    m_hoverOcrWorker->noteSelectionRequest(token);
    qInfo().noquote() << "[Hover] selection endpoints=" << selected
                      << "readRect=" << readRect << "image=" << image.size()
                      << "request=" << token;
    const bool queued = QMetaObject::invokeMethod(
        m_hoverOcrWorker, "recognizeSelection", Qt::QueuedConnection,
        Q_ARG(QString, QString::number(token)), Q_ARG(QImage, image),
        Q_ARG(qint64, EZTranslator::ocrMonotonicMs()));
    if (!queued)
        m_selectionActive = false;
}

void AppController::onSelectionRecognized(const QString& id, const QString& text)
{
    if (!m_selectionActive || id.toULongLong() != m_selectionToken
        || m_state != EZTranslator::TranslationState::Running || m_overlayMode)
        return;
    if (text.isEmpty()) {
        qInfo() << "[Hover] selection OCR empty" << id;
        m_selectionActive = false;
        m_mainWindow->setStatus(QStringLiteral("Không nhận diện được chữ trong vùng đã chọn"),
                                QColor("#f59e0b"));
        return;
    }
    const QRect clientRect = m_selectionScreenRect.translated(-m_selectionFrameClient.topLeft());
    showHoverLine(text, clientRect, m_selectionScreenRect.topRight());
}

void AppController::showHoverLine(const QString& text, const QRect& lineClient,
                                  const QPoint& cursorScreen)
{
    if (!m_hoverPopup || text.isEmpty())
        return;
    // Đang Overlay: popup hover TUYỆT ĐỐI không hiện (hai chế độ độc lập).
    if (m_overlayMode)
        return;

    m_hoverSource = text;
    m_hoverLineValid = true;
    m_hoverLineClient = lineClient;
    m_hoverLineCursor = cursorScreen;

    const QPoint logical = cursorToLogical(cursorScreen);
    const int sourceWidth = lineClient.isNull() ? 0 : sizeToLogical(lineClient.width());

    // Đã dịch dòng này rồi → hiện MỘT LẦN (nguồn + bản dịch) ngay lập tức.
    const QString cached = m_hoverCache.value(text);
    if (!cached.isEmpty()) {
        m_hoverPending = false;
        m_hoverTranslation = cached;
        m_hoverPopup->showTranslation(text, cached, logical, sourceWidth,
                                      m_selectionActive ? hoverSelectionLogicalRect() : QRect());
        return;
    }

    // Chưa có bản dịch: KHÔNG hiện dòng gốc trước (tránh "hai giai đoạn"); đợi worker
    // dịch xong rồi hiện đồng thời ở onTranslationReady.
    m_hoverTranslation.clear();
    m_hoverPending = true;
    m_hoverProbeMs = m_clock.isValid() ? m_clock.elapsed() : 0;

    if (!ensureTranslator()) {
        m_hoverPending = false;
        m_hoverPopup->showTranslation(text, QString(), logical, sourceWidth,
                                      m_selectionActive ? hoverSelectionLogicalRect() : QRect());
        return;
    }

    // Token RIÊNG cho lượt dịch hover. Trước đây dùng chung m_hoverToken với probe nên
    // mỗi lần tick dò lại (350ms) là token nhảy ⇒ kết quả dịch đang về bị coi là "cũ" và
    // bị bỏ, trong khi m_hoverPending vẫn true ⇒ hover đứng im mãi ở đoạn đó ("đơ ko dịch").
    const quint64 token = ++m_hoverTranslationToken;
    // Báo worker biết có yêu cầu ưu tiên cao: lượt dịch vùng đang chạy sẽ nhường
    // event loop giữa các khối để hover được dịch trước.
    if (m_translateWorker)
        m_translateWorker->markInteractivePending();
    QMetaObject::invokeMethod(m_translateWorker, "translateInteractive", Qt::QueuedConnection,
                              Q_ARG(QString, kHoverId), Q_ARG(QString, text),
                              Q_ARG(quint64, token),
                              Q_ARG(qint64, EZTranslator::ocrMonotonicMs()));
}

void AppController::onHoverProbed(const QString& id, const QString& text, const QRect& lineRect,
                                  bool clippedByBand)
{
    const quint64 token = id.toULongLong();
    const auto geomIt = m_hoverProbes.constFind(token);
    if (geomIt == m_hoverProbes.constEnd()) {
        m_hoverProbeRunning = false;
        flushPendingHoverProbe();
        return; // probe cũ / không rõ hình học
    }
    const HoverProbeGeom geom = geomIt.value();
    m_hoverProbes.erase(geomIt);
    m_hoverProbeRunning = false;

    if (m_selectionButtonDown || m_selectionActive) {
        flushPendingHoverProbe();
        return;
    }

    if (!m_hoverPopup) {
        flushPendingHoverProbe();
        return;
    }

    const bool hasLine = !lineRect.isNull() && !text.isEmpty();

    // Cụm chữ con trỏ đang ở vẫn CHẠM mép dải ⇒ câu còn dài hơn dải: MỞ RỘNG dải rồi dò
    // lại (tối đa vài bước). Dừng khi cụm kết thúc ở KHE — biên tự nhiên do khoảng đệm
    // tới phần tử khác quyết định, đúng cách meikipop làm (họ đọc cả vùng nên không cắt).
    // Chỉ làm cho probe "cuối"; probe dò trước (con trỏ đang đi) không tốn thêm gì.
    if (hasLine && clippedByBand && !geom.prefetch && geom.canGrow
        && geom.stage + 1 < kBandStageCount) {
        qDebug() << "[Hover] cụm chạm mép dải → mở rộng bước" << (geom.stage + 1);
        sendHoverProbe(geom.cursorScreen, /*prefetch=*/false, geom.stage + 1);
        flushPendingHoverProbe();
        return;
    }

    // Luôn ghi lại kết quả dò trước để lần con trỏ dừng kế tiếp dùng ngay.
    if (hasLine) {
        const QRect lineFrame = lineRect.translated(geom.bandOrigin);
        m_hoverPrefetch.text = text;
        m_hoverPrefetch.lineClient =
            QRect(int(lineFrame.x() / geom.scaleX), int(lineFrame.y() / geom.scaleY),
                  int(lineFrame.width() / geom.scaleX), int(lineFrame.height() / geom.scaleY));
        m_hoverPrefetch.atMs = m_clock.isValid() ? m_clock.elapsed() : 0;
    }

    // Dò trước: chỉ để dành, KHÔNG hiện popup (con trỏ còn đang di chuyển).
    const qint64 nowMs = m_clock.isValid() ? m_clock.elapsed() : 0;

    if (geom.prefetch) {
        flushPendingHoverProbe();
        return;
    }
    if (token != m_hoverLastFinalToken) {
        flushPendingHoverProbe();
        return; // đã có yêu cầu cuối mới hơn
    }

    if (!hasLine) {
        // Debounce: mot probe rong (frame dang chuyen, vung trong, OCR nham)
        // khong du de an popup - neu an ngay thi popup nhap nhay theo con tro.
        ++m_hoverEmptyStreak;
        if (m_hoverEmptyStreak >= 3 || nowMs - m_hoverLastValidMs >= 400) {
            m_hoverPopup->hidePopup();
            m_hoverLineValid = false;
            m_hoverPending = false;
            m_hoverEmptyStreak = 0;
        }
        flushPendingHoverProbe();
        return;
    }

    m_hoverEmptyStreak = 0;
    m_hoverLastValidMs = nowMs;
    if (text == m_hoverSource && m_hoverLineValid
        && (m_hoverPending || !m_hoverTranslation.isEmpty())) {
        flushPendingHoverProbe();
        return;
    }

    m_hoverProbedCursor = geom.cursorScreen;
    showHoverLine(text, m_hoverPrefetch.lineClient, geom.cursorScreen);
    flushPendingHoverProbe();
}

void AppController::start()
{
    m_mainWindow->show();
    m_mainWindow->raise();
    m_mainWindow->activateWindow();

    // NẠP SẴN ENGINE (chạy nền, không chặn UI) ngay khi mở app: model OCR (DirectML init) và
    // sidecar dịch (Python + CTranslate2) tốn vài giây cold start — trước đây bản dịch THẬT đầu
    // tiên phải chờ >5s sau khi bấm Bắt đầu. Nạp trước thì bấm là dịch được ngay.
    QTimer::singleShot(0, this, [this]() { preloadEngines(); });
}

void AppController::preloadEngines()
{
    // OCR: hai worker (vùng + hover) tự nạp model trên luồng riêng — UI không đứng.
    startOcr();

    // Dịch: dựng worker dịch; `configure` (chạy trên luồng worker) nạp trước ct2/Marian en→vi
    // kể cả khi nguồn còn để "auto" (xem TranslationWorker::configure) — sidecar Python khởi
    // động + nạp model trong lúc người dùng còn chọn cửa sổ, không rơi vào bản dịch đầu tiên.
    ensureTranslator();
}

void AppController::onStartTranslation()
{
    if (m_mainWindow->selectedWindowHandle() == 0) {
        m_mainWindow->showSelectWindowWarning();
        return;
    }

    // Đồng bộ chế độ hiển thị từ UI TRƯỚC khi bật bất kỳ đường nào — đảm bảo overlay và cửa sổ
    // nổi không chạy cùng lúc (cờ có thể lệch nếu UI đổi chế độ mà signal bị lỡ).
    const MainWindow::DisplayMode activeMode = m_mainWindow->displayMode();
    m_overlayMode = activeMode != MainWindow::DisplayMode::HoverTranslate;
    m_floatingMode = activeMode == MainWindow::DisplayMode::FloatingWindow;

    m_state = EZTranslator::TranslationState::Running;
    m_mainWindow->onTranslationStarted();
    m_trayManager->updateState(true);

    if (m_floatingWindow) {
            m_floatingWindow->setSourceText(QString());
        m_floatingWindow->setTranslatedText(QString());
        if (m_floatingMode) {
            m_floatingWindow->show();
            m_floatingWindow->raise();
            m_floatingWindow->activateWindow();
        } else {
            m_floatingWindow->hide();
        }
    }

    // Capture chạy trước (nhẹ) để pipeline hoạt động sớm; nạp model OCR (nặng: DirectML
    // init + warm-up) ở BACKGROUND nên UI không đứng vài giây như trước.
    if (m_capture) {
        startCapture(m_mainWindow->selectedWindowHandle());
    }

#ifdef _WIN32
    // Hover chi chay khi cua so dich o tien canh. Khi nguoi dung bam Bat dau,
    // chu dong dua cua so dich len truoc de WGC co frame ngay tu lan dau.
    {
        const quintptr handle = m_mainWindow->selectedWindowHandle();
        QTimer::singleShot(150, this, [this, handle]() {
            if (m_state != EZTranslator::TranslationState::Running)
                return;
            HWND hwnd = reinterpret_cast<HWND>(handle);
            if (!hwnd || !IsWindow(hwnd))
                return;
            if (IsIconic(hwnd))
                ShowWindow(hwnd, SW_RESTORE);
            if (GetForegroundWindow() != hwnd)
                SetForegroundWindow(hwnd);
        });
    }
#endif
    if (m_visibilityTimer)
        m_visibilityTimer->start();
    if (m_catchUpTimer)
        m_catchUpTimer->start();

    if (m_ocrReady.load()) {
        if (!m_overlayMode)
            startHover();
    } else {
        startOcr();
    }

    // WARM-UP engine dịch: sidecar CTranslate2 mất 1-3s cho lần khởi động đầu (nạp model Python).
    // Chạy sẵn một câu ngay khi bật dịch để bản dịch THẬT đầu tiên không phải chờ cold start.
    if (ensureTranslator()) {
        QMetaObject::invokeMethod(m_translateWorker, "translate", Qt::QueuedConnection,
                                  Q_ARG(QString, QStringLiteral("__warmup__")),
                                  Q_ARG(QString, QStringLiteral("Hello.")), Q_ARG(quint64, 0),
                                  Q_ARG(qint64, EZTranslator::ocrMonotonicMs()));
    }
}

void AppController::onStopTranslation()
{
    m_state = EZTranslator::TranslationState::Idle;
    m_mainWindow->onTranslationStopped();
    m_trayManager->updateState(false);

    if (m_floatingWindow) {
        m_floatingWindow->hide();
            m_floatingWindow->setSourceText(QString());
        m_floatingWindow->setTranslatedText(QString());
    }

    if (m_textOverlay)
        m_textOverlay->hideOverlay();

    // "Tự động xóa cửa sổ" (Cài đặt → Giao diện): dừng dịch là xoá luôn nội dung + cache
    // theo nội dung của mọi vùng — lần bật sau quét lại từ đầu thay vì hiện lại bản cũ.
    if (m_settings.autoClearWindow) {
        for (const EZTranslator::TranslationRegion& region : m_regions)
            forgetRegion(region.id);
        m_ocrBoxCache.clear();
        m_regionOcrHash.clear();
        m_regionOcrContentHash.clear();
        m_regionServedContentHash.clear();
        m_regionRequestSource.clear();
        m_pendingSourceBoxes.clear();
        m_overlaySuspended = false;
    }

    if (m_visibilityTimer)
        m_visibilityTimer->stop();
    if (m_catchUpTimer)
        m_catchUpTimer->stop();

    stopHover();
    clearVisualCaches();

    if (m_capture) {
        if (!m_roiPreviewDialog || !m_roiPreviewDialog->isVisible()) {
            m_capture->stop();
            m_captureStartedForPreview = false;
        } else {
            m_captureStartedForPreview = true;
        }
    }
}

void AppController::onToggleTranslation()
{
    if (m_state == EZTranslator::TranslationState::Running) {
        onStopTranslation();
    } else {
        onStartTranslation();
    }
}

void AppController::onOpenRegionEditor()
{
    m_editRegionIndex = -1; // mở từ nút "Chọn vùng"/editor = thêm vùng mới
    startRegionSnipping();
}

void AppController::onRequestAddRegion()
{
    m_editRegionIndex = -1;
    startRegionSnipping();
}

void AppController::onRequestEditRegion(int index)
{
    if (index < 0 || index >= m_regions.size())
        return;
    m_editRegionIndex = index;
    startRegionSnipping();
}

/**
 * @brief Tạm ẩn MainWindow, chụp màn hình rồi mở lớp khoanh vùng (snipper).
 *
 * Dùng chung cho Thêm vùng / Sửa vùng / editor. Ai gọi thì đặt trước
 * `m_editRegionIndex` để quyết định kết quả khoanh sẽ THÊM hay THAY vùng.
 */
void AppController::startRegionSnipping()
{
    const quintptr handle = m_mainWindow->selectedWindowHandle();
    if (handle == 0) {
        m_mainWindow->showSelectWindowWarning();
        return;
    }

    QRect targetRect;

#ifdef _WIN32
    if (handle != 0) {
        HWND targetHwnd = reinterpret_cast<HWND>(handle);
        if (IsWindow(targetHwnd)) {
            HWND rootHwnd = GetAncestor(targetHwnd, GA_ROOT);
            if (rootHwnd && IsWindow(rootHwnd)) {
                targetHwnd = rootHwnd;
            }

            if (IsIconic(targetHwnd)) {
                ShowWindow(targetHwnd, SW_RESTORE);
            }
            SetForegroundWindow(targetHwnd);
            BringWindowToTop(targetHwnd);

            // Lấy client area (nội dung thực tế cần dịch) sang toạ độ màn hình (physical px)
            RECT clientRc = {};
            GetClientRect(targetHwnd, &clientRc);
            POINT pt = {0, 0};
            ClientToScreen(targetHwnd, &pt);

            int physX = pt.x;
            int physY = pt.y;
            int physW = clientRc.right - clientRc.left;
            int physH = clientRc.bottom - clientRc.top;

            // Chuyển sang toạ độ logical của Qt theo tỉ lệ High-DPI của màn hình
            qreal dpr = 1.0;
            if (QScreen* screen = QGuiApplication::primaryScreen()) {
                dpr = screen->devicePixelRatio();
            }
            if (dpr <= 0.0) dpr = 1.0;

            targetRect = QRect(
                qRound(physX / dpr),
                qRound(physY / dpr),
                qRound(physW / dpr),
                qRound(physH / dpr)
            );
            qDebug() << "[AppController] TargetWindow:" << (void*)targetHwnd
                     << "physClient:" << physX << physY << physW << physH
                     << "dpr:" << dpr << "targetRect:" << targetRect;
        }
    }
#endif

    // 1. Tạm ẩn MainWindow để chụp màn hình
    m_mainWindow->hide();

    // 2. Mở overlay sau một khoảng trễ ngắn
    QTimer::singleShot(200, this, [this, targetRect]() {
        QPixmap screenshot;
        if (QScreen* screen = QGuiApplication::primaryScreen()) {
            screenshot = screen->grabWindow(0);
        }
        m_snipperOverlay->startSnipping(screenshot, targetRect);
    });
}

void AppController::onRegionSnapped(const EZTranslator::NormalizedRect& rect, const QRect& screenRect)
{
    EZTranslator::TranslationRegion reg;

    const bool editing = m_editRegionIndex >= 0 && m_editRegionIndex < m_regions.size();
    if (editing) {
        // Sửa vùng đang chọn: giữ id/tên/màu, chỉ thay hình học.
        reg = m_regions.at(m_editRegionIndex);
        reg.normalizedRect = rect;
    } else {
        // Thêm vùng mới: giữ nguyên các vùng đang có.
        reg.id = QStringLiteral("roi_%1").arg(m_nextRegionNumber++);
        reg.orderNumber = m_regions.size() + 1;
        reg.name = QStringLiteral("Vùng dịch %1").arg(reg.orderNumber);
        reg.normalizedRect = rect;
        reg.tagColor = QColor(37, 99, 235);
    }

    const QString regionId = reg.id;
    if (editing)
        m_regions[m_editRegionIndex] = reg;
    else
        m_regions.append(reg);

    // Hình học đổi ⇒ mọi kết quả OCR/dịch cũ của vùng này không còn giá trị.
    forgetRegion(regionId);
    m_regionScreenRects.insert(regionId, screenRect);

    m_regionManager.setRegions(m_regions);
    m_ocrScheduler.reset();
    m_currentScreenRect = screenRect;
    m_currentRegionIndex = editing ? m_editRegionIndex : m_regions.size() - 1;
    m_editRegionIndex = -1;

    if (m_highlightOverlay && m_highlightOverlay->isVisible())
        m_highlightOverlay->hideOverlay();

    m_mainWindow->show();
    m_mainWindow->raise();
    m_mainWindow->activateWindow();
#ifdef _WIN32
    SetForegroundWindow(reinterpret_cast<HWND>(m_mainWindow->winId()));
#endif
    m_mainWindow->setRegion(rect, screenRect);
    m_mainWindow->setShowRegionActive(false);
    refreshRegionList();

    m_mainWindow->setStatus(editing ? QStringLiteral("Đã cập nhật vùng dịch đang chọn")
                                    : QStringLiteral("Đã thêm vùng dịch mới (%1 vùng)")
                                          .arg(m_regions.size()),
                            QColor("#38bdf8"));
}

void AppController::onRequestRemoveRegion(int index)
{
    if (index < 0 || index >= m_regions.size())
        return;

    const QString removedId = m_regions.at(index).id;
    m_regions.removeAt(index);

    // Đánh số lại để tên/id hiển thị vẫn tuần tự sau khi xoá.
    for (int i = 0; i < m_regions.size(); ++i) {
        m_regions[i].orderNumber = i + 1;
        if (m_regions.at(i).name.startsWith(QStringLiteral("Vùng dịch")))
            m_regions[i].name = QStringLiteral("Vùng dịch %1").arg(i + 1);
    }

    forgetRegion(removedId);
    m_regionManager.setRegions(m_regions);
    m_ocrScheduler.reset();
    m_currentRegionIndex = m_regions.isEmpty() ? -1 : qMin(index, m_regions.size() - 1);

    if (m_regions.isEmpty() && m_textOverlay)
        m_textOverlay->hideOverlay();

    refreshRegionList();
    m_mainWindow->setStatus(m_regions.isEmpty()
                                ? QStringLiteral("Đã xoá vùng cuối — quay về dịch toàn bộ cửa sổ")
                                : QStringLiteral("Đã xoá vùng (%1 vùng còn lại)").arg(m_regions.size()),
                            QColor("#f59e0b"));
}

void AppController::onRegionSelected(int index)
{
    if (index < 0 || index >= m_regions.size()) {
        m_currentRegionIndex = -1;
        return;
    }
    m_currentRegionIndex = index;

    const EZTranslator::TranslationRegion& region = m_regions.at(index);
    const QRect screenRect = m_regionScreenRects.value(region.id);
    m_mainWindow->setRegion(region.normalizedRect, screenRect);

    if (m_highlightOverlay && m_highlightOverlay->isVisible())
        m_highlightOverlay->hideOverlay();
    m_mainWindow->setShowRegionActive(false);
    if (m_floatingMode)
        refreshFloatingWindow();
}

void AppController::refreshRegionList()
{
    if (m_currentRegionIndex >= m_regions.size())
        m_currentRegionIndex = m_regions.isEmpty() ? -1 : m_regions.size() - 1;
    if (m_regions.isEmpty())
        m_currentRegionIndex = -1;

    m_mainWindow->setRegions(m_regions, m_currentRegionIndex);
}

void AppController::forgetRegion(const QString& regionId)
{
    m_regionBoxes.remove(regionId);
    m_regionSourceText.remove(regionId);
    m_regionTranslatedText.remove(regionId);
    m_regionLines.remove(regionId);
    m_regionUiBoxes.remove(regionId);
    m_regionNovelParagraphs.remove(regionId);
    m_regionScrollReuseEligible.remove(regionId);
    m_regionStateAtMs.remove(regionId);
    m_regionServedHash.remove(regionId);
    m_regionOcrContentHash.remove(regionId);
    m_regionServedContentHash.remove(regionId);
    m_regionServedProfile.remove(regionId);
    m_regionObservedProfile.remove(regionId);
    m_regionDisplayProfile.remove(regionId);
    m_regionFrameProfile.remove(regionId);
    m_regionOverlayTrackers.remove(regionId);
    m_regionLastScrollMatchMs.remove(regionId);
    m_regionScrolledOverlay.remove(regionId);
    m_regionVerifyPending.remove(regionId);
    m_regionLineTexts.remove(regionId);
    m_regionToken.remove(regionId);
    m_regionOcrInfo.remove(regionId);
    m_regionOcrToken.remove(regionId);
    m_regionOcrMailbox.remove(regionId);
    m_regionLinePitch.remove(regionId);
    m_regionParagraphRatio.remove(regionId);
    m_regionImages.remove(regionId);
    m_regionPlates.remove(regionId);
    m_regionOcrAtMs.remove(regionId);
    m_regionTranslateAtMs.remove(regionId);
    m_regionScreenRects.remove(regionId);
}

void AppController::clearVisualCaches()
{
    // Plate la anh full-vung, m_regionImages la ban mau; giu lai sau khi stop
    // chi ton RAM. Text cache dich (TranslationCache) van duoc giu rieng.
    m_ocrBoxCache.clear();
    m_regionPlates.clear();
    m_regionImages.clear();
    m_pendingSourceBoxes.clear();
    m_regionBoxes.clear();

    m_regionLines.clear();
    m_regionUiBoxes.clear();
    m_regionNovelParagraphs.clear();
    m_regionScrollReuseEligible.clear();
    m_regionLineTexts.clear();
    m_regionSourceText.clear();
        m_regionTranslatedText.clear();
    m_paragraphTranslations.clear();
    m_regionStateAtMs.clear();
    m_regionRequestSource.clear();
    m_regionToken.clear();
    m_regionOcrHash.clear();
    m_regionOcrContentHash.clear();
    m_regionServedContentHash.clear();
    m_regionOcrInfo.clear();
    m_regionOcrToken.clear();
    m_regionOcrMailbox.clearPending();
    m_regionOcrAtMs.clear();
    m_regionTranslateAtMs.clear();
    m_regionLastChangeMs.clear();
    m_regionServedHash.clear();
    m_regionServedProfile.clear();
    m_regionObservedProfile.clear();
    m_regionDisplayProfile.clear();
    m_regionFrameProfile.clear();
    m_regionOverlayTrackers.clear();
    m_regionLastScrollMatchMs.clear();
    m_regionScrolledOverlay.clear();
    m_regionLinePitch.clear();
    m_regionParagraphRatio.clear();
    m_ocrScheduler.reset();

    if (m_textOverlay) {
        m_textOverlay->clear();
        m_textOverlay->hideOverlay();
    }
}

void AppController::onRegionSnapCancelled()
{
    m_editRegionIndex = -1; // huỷ khoanh ⇒ không thay đổi vùng nào
    m_mainWindow->show();
    m_mainWindow->raise();
    m_mainWindow->activateWindow();
#ifdef _WIN32
    SetForegroundWindow(reinterpret_cast<HWND>(m_mainWindow->winId()));
#endif
}

void AppController::onShowRegion()
{
    // Toggle: nếu overlay đang hiển thị thì ẩn đi
    if (m_highlightOverlay->isVisible()) {
        m_highlightOverlay->hideOverlay();
        m_mainWindow->setShowRegionActive(false);
        return;
    }

    if (!m_mainWindow->hasRegion()) {
        m_mainWindow->setStatus("Chưa chọn vùng dịch! Bấm 'Chọn vùng (F6)' để quét vùng.", QColor("#f59e0b"));
        return;
    }

    // 1. Ưu tiên tọa độ màn hình thực tế (pixel tuyệt đối) của VÙNG ĐANG CHỌN
    // Đảm bảo 100% khớp tuyệt đối với vị trí vừa chọn trên màn hình
    QRect screenRect;
    if (m_currentRegionIndex >= 0 && m_currentRegionIndex < m_regions.size()) {
        screenRect = m_regionScreenRects.value(m_regions.at(m_currentRegionIndex).id);
    }
    if (!screenRect.isValid())
        screenRect = m_mainWindow->currentScreenRect();
    if (!screenRect.isValid() && m_currentScreenRect.isValid()) {
        screenRect = m_currentScreenRect;
    }

    if (screenRect.isValid() && screenRect.width() >= 10 && screenRect.height() >= 10) {
        m_highlightOverlay->showScreenRect(screenRect, 8000);
        m_mainWindow->setShowRegionActive(true);
        return;
    }

    // 2. Fallback: nếu chỉ có tọa độ chuẩn hóa (NormalizedRect)
    EZTranslator::NormalizedRect roi = m_mainWindow->currentRegion();
    if (m_currentRegionIndex >= 0 && m_currentRegionIndex < m_regions.size())
        roi = m_regions.at(m_currentRegionIndex).normalizedRect;
    QRect targetRect;
#ifdef _WIN32
    quintptr handle = m_mainWindow->selectedWindowHandle();
    if (handle != 0) {
        HWND targetHwnd = reinterpret_cast<HWND>(handle);
        if (IsWindow(targetHwnd)) {
            RECT clientRc = {};
            GetClientRect(targetHwnd, &clientRc);
            POINT pt = {0, 0};
            ClientToScreen(targetHwnd, &pt);

            int physX = pt.x;
            int physY = pt.y;
            int physW = clientRc.right - clientRc.left;
            int physH = clientRc.bottom - clientRc.top;

            qreal dpr = 1.0;
            if (QScreen* screen = QGuiApplication::primaryScreen()) {
                dpr = screen->devicePixelRatio();
            }
            if (dpr <= 0.0) dpr = 1.0;

            targetRect = QRect(
                qRound(physX / dpr),
                qRound(physY / dpr),
                qRound(physW / dpr),
                qRound(physH / dpr)
            );
        }
    }
#endif

    if (targetRect.isEmpty() || targetRect.width() < 100 || targetRect.height() < 100) {
        if (QScreen* s = QGuiApplication::primaryScreen()) {
            targetRect = s->geometry();
        }
    }

    m_highlightOverlay->showRegion(targetRect, roi, 8000);
    m_mainWindow->setShowRegionActive(true);
}

void AppController::onPreviewRoiRequested()
{
    quintptr handle = m_mainWindow->selectedWindowHandle();
    if (handle == 0) {
        m_mainWindow->showSelectWindowWarning();
        return;
    }

    if (m_regionManager.isEmpty()) {
        m_mainWindow->setStatus("Chưa chọn vùng dịch! Bấm 'Chọn vùng' để tạo ROI.", QColor("#f59e0b"));
        return;
    }

    if (!m_roiPreviewDialog) {
        m_roiPreviewDialog = new EZTranslator::RoiPreviewDialog(nullptr);
        connect(m_roiPreviewDialog, &EZTranslator::RoiPreviewDialog::closed,
                this, &AppController::onPreviewClosed);
    }

    if (m_state != EZTranslator::TranslationState::Running && m_capture) {
        if (!m_capture->isRunning()) {
            startCapture(handle);
            m_captureStartedForPreview = true;
        }
    }

    m_roiPreviewDialog->show();
    m_roiPreviewDialog->raise();
    m_roiPreviewDialog->activateWindow();
}

void AppController::onPreviewClosed()
{
    if (m_captureStartedForPreview) {
        if (m_capture && m_state != EZTranslator::TranslationState::Running) {
            m_capture->stop();
        }
        m_captureStartedForPreview = false;
    }
}

void AppController::onRegionEditorSaved(const QList<EZTranslator::TranslationRegion>& regions)
{
    // Editor có thể đổi cả danh sách → trạng thái OCR/dịch của mọi vùng cũ đều có thể sai.
    for (const EZTranslator::TranslationRegion& region : m_regions)
        forgetRegion(region.id);

    m_regions = regions;
    for (int i = 0; i < m_regions.size(); ++i)
        m_regions[i].orderNumber = i + 1;

    // Id cũ có thể đã bị editor đổi/đánh lại số: cấp lại id trùng khớp số thứ tự để
    // dropdown, RegionManager và mọi hash trạng thái luôn nói về cùng một tập vùng.
    QSet<QString> usedIds;
    for (int i = 0; i < m_regions.size(); ++i) {
        QString id = m_regions.at(i).id;
        if (id.isEmpty() || usedIds.contains(id))
            id = QStringLiteral("roi_%1").arg(i + 1);
        usedIds.insert(id);
        m_regions[i].id = id;
        if (m_regions.at(i).name.isEmpty())
            m_regions[i].name = QStringLiteral("Vùng dịch %1").arg(i + 1);
    }

    m_regionManager.setRegions(m_regions);
    m_ocrScheduler.reset();
    m_currentRegionIndex = m_regions.isEmpty() ? -1 : 0;
    m_editRegionIndex = -1;
    m_nextRegionNumber = m_regions.size() + 1;

    if (m_regions.isEmpty() && m_textOverlay)
        m_textOverlay->hideOverlay();

    refreshRegionList();
    if (!m_regions.isEmpty())
        onRegionSelected(m_currentRegionIndex);

    m_regionEditor->hide();
    m_mainWindow->show();
    m_mainWindow->raise();
    m_mainWindow->activateWindow();
}

void AppController::onRegionEditorCancelled()
{
    m_regionEditor->hide();
    m_mainWindow->show();
    m_mainWindow->raise();
    m_mainWindow->activateWindow();
}

void AppController::onOpenSettings()
{
    m_settingsDialog->setSettings(m_settings);
    m_settingsDialog->show();
    m_settingsDialog->raise();
    m_settingsDialog->activateWindow();
}

void AppController::onSettingsSaved(const EZTranslator::AppSettings& settings)
{
    // Ngôn ngữ do MainWindow quản lý; SettingsDialog không có control cho cặp này,
    // nên giữ giá trị hiện tại (tránh dialog mở lâu làm revert lựa chọn mới).
    EZTranslator::AppSettings updated = settings;
    updated.sourceLanguage = m_settings.sourceLanguage;
    updated.targetLanguage = m_settings.targetLanguage;

    // Đổi engine/đường dẫn model OCR cần nạp lại model (nặng) — chỉ khi app đã từng nạp.
    const bool ocrReloadNeeded = updated.ocrEngine != m_settings.ocrEngine
                                 || updated.ocrUseGpu != m_settings.ocrUseGpu
                                 || updated.ocrDetDevice != m_settings.ocrDetDevice
                                 || updated.ocrRecDevice != m_settings.ocrRecDevice
                                 || updated.ocrBinarize != m_settings.ocrBinarize;
    const bool novelModeChanged = updated.novelMode != m_settings.novelMode;
    const bool gameDialogModeChanged = updated.gameDialogMode != m_settings.gameDialogMode;
    const bool translationEngineChanged =
        updated.translationEngine != m_settings.translationEngine;

    m_settings = updated;
    EZTranslator::SettingsStore::save(m_settings);
    m_hoverCache.clear(); // engine/prompt có thể đổi → không dùng bản dịch hover cũ
    if (translationEngineChanged)
        m_paragraphTranslations.clear();

    if (gameDialogModeChanged) {
        m_ocrScheduler.reset();
        m_regionServedContentHash.clear();
        m_regionOcrMailbox.clearPending();
        for (auto it = m_regionOcrToken.begin(); it != m_regionOcrToken.end(); ++it) {
            it.value() = 0;
            if (m_ocrWorker)
                m_ocrWorker->noteRegionRequest(it.key(), 0);
            m_regionOcrMailbox.remove(it.key());
        }
        qInfo() << "[OCR_MODE] gameDialog=" << m_settings.gameDialogMode;
    }

    applySettings(/*initial=*/false);

    // Cài đặt có thể đổi chế độ hiển thị → đồng bộ lại (tắt hover nếu đang Overlay…).
    syncOverlayMode();

    if (novelModeChanged) {
        // Force a fresh OCR/layout pass; otherwise stale boxes keep the old
        // mode until the user stops and restarts the overlay.
        m_regionBoxes.clear();
        m_regionDisplayProfile.clear();
        m_regionFrameProfile.clear();
        m_regionOverlayTrackers.clear();
        m_regionLastScrollMatchMs.clear();
        m_regionScrolledOverlay.clear();
        m_regionNovelParagraphs.clear();
        m_regionScrollReuseEligible.clear();
        m_regionUiBoxes.clear();
        m_regionLines.clear();
        m_regionLineTexts.clear();
        m_regionSourceText.clear();
        m_regionTranslatedText.clear();
    m_paragraphTranslations.clear();
        m_regionServedHash.clear();
        m_regionOcrContentHash.clear();
        m_regionServedContentHash.clear();
        m_ocrScheduler.reset();
        if (m_textOverlay)
            m_textOverlay->clear();
    }

    if (ocrReloadNeeded && m_ocrLoadAttempted)
        reloadOcr();

    m_mainWindow->setStatus(QStringLiteral("Đã lưu cài đặt"), QColor("#38bdf8"));
}

void AppController::applySettings(bool initial)
{
    Q_UNUSED(initial);

    // ── Kiểu chữ lớp overlay (Cài đặt → Giao diện) ───────────────────────────
    // pt → pixel frame: overlay vẽ theo pixel của khung capture (96 DPI chuẩn), nên
    // nhân 96/72; khi hiển thị paintEvent tự scale theo tỉ lệ widget/frame.
    const int fontPx = m_settings.keepSourceFormatting
                           ? 0
                           : qRound(m_settings.fontSize * 96.0 / 72.0);
    const int lineSpacingPx = m_settings.lineSpacing > 0
                                  ? qRound(m_settings.lineSpacing * 96.0 / 72.0)
                                  : 0;
    if (m_textOverlay) {
        m_textOverlay->setCoverColor(m_settings.windowColor);
        m_textOverlay->setTextStyle(m_settings.fontColor, m_settings.isBold, fontPx,
                                    m_settings.textAlignment, lineSpacingPx);
        m_textOverlay->setContentOpacityPercent(m_settings.windowOpacity);
        m_textOverlay->setExcludeFromCapture(m_settings.excludeFromCapture);
    }
    // Popup hover + thanh nổi cũng nằm trên cửa sổ đích → cùng bị loại khỏi capture.
    if (m_hoverPopup) {
        m_hoverPopup->setAppearance(m_settings);
        EZTranslator::Platform::setExcludedFromCapture(quintptr(m_hoverPopup->winId()),
                                                       m_settings.excludeFromCapture);
    }
    if (m_floatingWindow)
        m_floatingWindow->setAppearance(m_settings);
    if (m_miniFloatBar)
        EZTranslator::Platform::setExcludedFromCapture(quintptr(m_miniFloatBar->winId()),
                                                       m_settings.excludeFromCapture);

    // ── Hiệu suất capture ────────────────────────────────────────────────────
    int captureFps = m_settings.captureFps;
    if (m_resourcePlan.profile == EZTranslator::ResourceProfile::Low)
        captureFps = qMin(captureFps, m_resourcePlan.captureFpsCap);
    if (m_wgcCapture)
        m_wgcCapture->setFps(captureFps);
    if (m_gdiCapture)
        m_gdiCapture->setFps(captureFps);

    // ── Khởi động cùng Windows (idempotent; cập nhật lại đường dẫn exe hiện tại) ──
    EZTranslator::Platform::setAutoStart(QStringLiteral("EZ-Translator"),
                                         QCoreApplication::applicationFilePath(),
                                         m_settings.startWithWindows);

    // ── Phím tắt toàn cục + chuỗi hiển thị trên UI ──────────────────────────
    applyHotkeys();

    // ── Thu nhỏ xuống khay khi đóng ─────────────────────────────────────────
    if (m_mainWindow)
        m_mainWindow->setCloseToTray(m_settings.minimizeToTrayOnClose);

    // ── Engine dịch: đẩy cấu hình mới cho worker (nếu đang chạy) ────────────
    m_translationConfig = translationConfig();
    if (m_translateWorker) {
        QMetaObject::invokeMethod(m_translateWorker, "configure", Qt::QueuedConnection,
                                  Q_ARG(EZTranslator::TranslationConfig, m_translationConfig));
    }
}

void AppController::applyHotkeys()
{
    qDeleteAll(m_hotkeyFallbacks);
    m_hotkeyFallbacks.clear();

    if (!m_hotkeys || !m_mainWindow)
        return;

    const QList<GlobalHotkeys::Binding> bindings = {
        { QStringLiteral("toggle"), QKeySequence(m_settings.toggleTranslationHotkey) },
        { QStringLiteral("selectRegion"), QKeySequence(m_settings.selectRegionHotkey) },
        { QStringLiteral("showRegion"), QKeySequence(m_settings.showRegionHotkey) },
        { QStringLiteral("settings"), QKeySequence(m_settings.openSettingsHotkey) },
    };

    const QStringList failed = m_hotkeys->apply(bindings, m_mainWindow->winId());

    // Phím bị app khác chiếm (hoặc OS từ chối) → QShortcut dự phòng: vẫn dùng được khi
    // cửa sổ app đang focus, kèm cảnh báo trên thanh trạng thái.
    QStringList fallbackNames;
    for (const GlobalHotkeys::Binding& binding : bindings) {
        if (binding.sequence.isEmpty() || !failed.contains(binding.id))
            continue;
        auto* shortcut = new QShortcut(binding.sequence, m_mainWindow);
        const QString id = binding.id;
        connect(shortcut, &QShortcut::activated, this, [this, id]() { dispatchHotkey(id); });
        m_hotkeyFallbacks.append(shortcut);
        fallbackNames.append(binding.sequence.toString(QKeySequence::NativeText));
    }

    if (!fallbackNames.isEmpty()) {
        qWarning() << "[Hotkeys] đăng ký toàn cục thất bại:" << fallbackNames;
        m_mainWindow->setStatus(
            QStringLiteral("Phím tắt %1 bị ứng dụng khác chiếm — chỉ dùng được khi cửa sổ này đang mở")
                .arg(fallbackNames.join(QStringLiteral(", "))),
            QColor("#f59e0b"));
    }

    m_mainWindow->setHotkeyHints(m_settings.toggleTranslationHotkey,
                                 m_settings.selectRegionHotkey);
    if (m_miniFloatBar)
        m_miniFloatBar->setStopHotkeyText(m_settings.toggleTranslationHotkey);
    if (m_trayManager)
        m_trayManager->setHotkeyText(m_settings.toggleTranslationHotkey);
}

void AppController::dispatchHotkey(const QString& id)
{
    if (id == QLatin1String("toggle")) {
        onToggleTranslation();
    } else if (id == QLatin1String("selectRegion")) {
        // Giống nút "Thêm vùng": phải chọn cửa sổ đích trước.
        if (m_mainWindow->selectedWindowHandle() == 0) {
            m_mainWindow->showSelectWindowWarning();
            return;
        }
        onRequestAddRegion();
    } else if (id == QLatin1String("showRegion")) {
        onShowRegion();
    } else if (id == QLatin1String("settings")) {
        onOpenSettings();
    }
}

void AppController::onLanguagesChanged(const QString& source, const QString& target)
{
    if (source.isEmpty() || target.isEmpty())
        return;
    if (source == m_settings.sourceLanguage && target == m_settings.targetLanguage)
        return;

    // Chỉ cần nạp lại OCR khi đổi giữa model tiếng Anh và model chung (ch).
    // Các cặp ja/ko/zh/vi đều dùng model ch, nên đổi qua lại không cần reload.
    const bool modelFamilyChanged =
        source.startsWith(QStringLiteral("en"), Qt::CaseInsensitive)
        != m_settings.sourceLanguage.startsWith(QStringLiteral("en"), Qt::CaseInsensitive);

    m_settings.sourceLanguage = source;
    m_settings.targetLanguage = target;
    m_detectedSource.clear();
    EZTranslator::SettingsStore::save(m_settings);

    // Bản dịch cũ gắn với cặp ngôn ngữ cũ → bỏ để OCR/dịch chạy lại từ đầu.
    m_hoverCache.clear();
    m_regionOcrInfo.clear();
    m_regionOcrToken.clear();
    m_regionOcrMailbox.clearPending();
    m_regionLinePitch.clear();
    m_regionParagraphRatio.clear();
    m_regionBoxes.clear();
    m_regionSourceText.clear();
    m_regionTranslatedText.clear();
    m_paragraphTranslations.clear();
    m_regionLines.clear();
    m_regionStateAtMs.clear();
    m_regionScrollReuseEligible.clear();
    m_regionServedHash.clear();
    m_regionOcrContentHash.clear();
    m_regionServedContentHash.clear();
    m_regionServedProfile.clear();
    m_regionObservedProfile.clear();
    m_regionDisplayProfile.clear();
    m_regionFrameProfile.clear();
    m_regionOverlayTrackers.clear();
    m_regionLastScrollMatchMs.clear();
    m_regionScrolledOverlay.clear();
    m_regionToken.clear();
    m_ocrScheduler.reset();

    m_hoverProbes.clear();
    m_hoverPrefetch = HoverPrefetch();
    m_hoverLineValid = false;
    m_hoverPending = false;
    if (m_hoverPopup)
        m_hoverPopup->hidePopup();
    if (m_textOverlay)
        m_textOverlay->hideOverlay();

    m_translationConfig = translationConfig();
    if (m_translateWorker) {
        QMetaObject::invokeMethod(m_translateWorker, "configure", Qt::QueuedConnection,
                                  Q_ARG(EZTranslator::TranslationConfig, m_translationConfig));
    }

    m_mainWindow->setStatus(
        QStringLiteral("Ngôn ngữ: %1 → %2").arg(languageLabel(source), languageLabel(target)),
        QColor("#38bdf8"));

    if (modelFamilyChanged && m_ocrLoadAttempted)
        reloadOcr();
}

void AppController::onShowMainWindow()
{
    m_mainWindow->show();
    m_mainWindow->raise();
    m_mainWindow->activateWindow();
}

void AppController::onQuit()
{
    qApp->quit();
}

void AppController::onTargetWindowSelected(quintptr handle, const QString& /*title*/, const QString& /*processName*/)
{
    m_selectionActive = false;
    m_selectionButtonDown = false;
    m_selectionFrame = QImage();
    m_selectionScreenRect = QRect();
    ++m_selectionToken;
    if (m_hoverOcrWorker)
        m_hoverOcrWorker->noteSelectionRequest(m_selectionToken);
    ++m_hoverTranslationToken;
    if (m_hoverPopup)
        m_hoverPopup->hidePopup();
    m_hoverFrameReady = false;
    clearVisualCaches();
    if (m_state == EZTranslator::TranslationState::Running || m_captureStartedForPreview) {
        if (m_capture) m_capture->stop();
        if (handle != 0) {
            startCapture(handle);
        }
    }
}

void AppController::onCaptureFrame(const EZTranslator::CapturedFrame& frame)
{
    // Backpressure: cho capture worker phat frame tiep theo sau khi ham nay
    // xu ly xong (ke ca return som), tranh hang tram QImage full-screen.
    struct FrameAckGuard {
        EZTranslator::IWindowCapture* capture;
        ~FrameAckGuard()
        {
            if (capture)
                capture->notifyFrameConsumed();
        }
    } frameAck{m_capture};

    if (frame.image.isNull() || frame.image.width() <= 0 || frame.image.height() <= 0) {
        // Frame rong/den tu backend: khong coi la "da co frame" va khong cho
        // hover dung frame cu.
        m_hoverFrameReady = false;
        m_hoverFrameSettle = 0;
        return;
    }

    m_gotFirstFrame = true;
    ++m_captureFramesSeen;
    const qint64 captureNowMs = m_clock.isValid() ? m_clock.elapsed() : 0;
    if (captureNowMs - m_captureDiagAtMs >= 2000) {
        m_captureDiagAtMs = captureNowMs;
        const QString backend = m_capture == m_wgcCapture ? QStringLiteral("WGC")
                                                           : QStringLiteral("GDI");
        qInfo().noquote() << "[CAPTURE_METRIC] backend=" << backend
                          << "frames=" << m_captureFramesSeen
                          << "size=" << frame.image.size()
                          << "bytes=" << frame.image.sizeInBytes()
                          << "fpsConfigured=" << (m_capture ? m_capture->fps() : 0);
    }
    const QImage previousCaptureImage = m_lastCapture.image;
    m_lastCapture = frame; // giữ lại cho timer đuổi kịp (cửa sổ tĩnh không phát frame mới)

    // Lưu frame mới nhất cho chế độ "dịch khi chỉ chuột" (dò dòng dưới con trỏ).
    const QSize hoverFrameSize = frame.image.size();
    const QRect hoverClient = targetClientRectPhysical();
    if (hoverFrameSize != m_hoverFrameSize || hoverClient != m_hoverClientPhysical) {
        // Doi kich thuoc/cua so/backend: phai on dinh lai roi moi probe.
        m_hoverFrameSettle = 0;
        m_hoverFrameReady = false;
    }
    m_hoverFrame = frame.image;
    m_hoverFrameSize = hoverFrameSize;
    m_hoverClientPhysical = hoverClient;
    if (hoverClient.isEmpty()) {
        m_hoverFrameReady = false;
        m_hoverFrameSettle = 0;
    } else {
        ++m_hoverFrameSettle;
        if (m_hoverFrameSettle >= 2)
            m_hoverFrameReady = true;
    }

    // Model OCR đang được nạp ở background → chưa xử lý gì (overlay để trống).
    if (!m_ocrReady.load()) {
        if (m_textOverlay && m_textOverlay->isShowing())
            m_textOverlay->hideOverlay();
        return;
    }

    // Cập nhật hoverFrame ở trên để probe con trỏ luôn có frame mới nhất, nhưng
    // không trích ROI, tính hash hoặc lập lịch OCR vùng trong chế độ hover.
    if (m_mainWindow->displayMode() == MainWindow::DisplayMode::HoverTranslate) {
        if (m_textOverlay && m_textOverlay->isShowing())
            m_textOverlay->hideOverlay();
        return;
    }

    QList<EZTranslator::RegionFrame> regionFrames;

    if (m_regionManager.isEmpty()) {
        if (!m_overlayMode) {
            if (m_textOverlay && m_textOverlay->isShowing())
                m_textOverlay->hideOverlay();
            return;
        }
        // Overlay mode + "toan bo cua so": coi ca frame la mot vung.
        EZTranslator::RegionFrame whole;
        whole.regionId = QStringLiteral("roi_full");
        whole.regionName = QStringLiteral("Toan bo cua so");
        whole.regionOrder = 1;
        whole.pixelRect = QRect(QPoint(0, 0), frame.image.size());
        whole.sourceFrameSize = frame.image.size();
        whole.image = frame.image;
        whole.timestamp = frame.timestamp;
        regionFrames.append(whole);
    } else {
        regionFrames = m_regionManager.extractRegions(frame);
    }

    // Cập nhật lên cửa sổ Preview ROI nếu người dùng đang mở
    if (m_roiPreviewDialog && m_roiPreviewDialog->isVisible()) {
        m_roiPreviewDialog->updateFrames(regionFrames);
    }

    m_ocrScheduler.beginTick();
    const qint64 now = m_clock.isValid() ? m_clock.elapsed() : 0;
    const bool forceVerifyFrame = m_forceVerifyFrame;

    QSize frameSize;
    for (const EZTranslator::RegionFrame& regionFrame : regionFrames) {
        if (!regionFrame.isValid())
            continue;

        frameSize = regionFrame.sourceFrameSize;

        // Hash nội dung vùng — dùng cho cả đường tăng dần lẫn cache box bên dưới.
        const quint64 regionHash = EZTranslator::imageDifferenceHash(regionFrame.image);
        QList<QRect> gameTextRects;
        if (m_settings.gameDialogMode) {
            for (const QRect& line : m_regionLines.value(regionFrame.regionId))
                gameTextRects.append(line.translated(-regionFrame.pixelRect.topLeft()));
            if (gameTextRects.isEmpty()) {
                for (const EZTranslator::OcrTextBox& box : m_regionUiBoxes.value(regionFrame.regionId))
                    gameTextRects.append(box.rect);
            }
        }
        const quint64 contentHash =
            m_settings.gameDialogMode
                ? EZTranslator::imageTextDifferenceHash(regionFrame.image, gameTextRects)
                : regionHash;
        if (m_settings.gameDialogMode && !forceVerifyFrame && !gameTextRects.isEmpty()
            && m_regionServedContentHash.value(regionFrame.regionId, 0) == contentHash) {
            ++m_gameDialogFramesSkipped;
            if (now - m_gameDialogSkipLogAtMs >= 2000) {
                m_gameDialogSkipLogAtMs = now;
                qInfo().noquote() << "[GAME_DIALOG] unchanged text; OCR skipped region="
                                  << regionFrame.regionId << "frames="
                                  << m_gameDialogFramesSkipped << "boxes=" << gameTextRects.size();
            }
            continue;
        }

        // Xac minh: scroll-tracking vua giu ban dich cu, nhung co the day la page-jump bi khop nham.
        // OCR lai dung frame hien tai, khong doi frame/change ke tiep.
        if (forceVerifyFrame && m_regionVerifyPending.contains(regionFrame.regionId)) {
            const QString& verifyId = regionFrame.regionId;
            m_regionVerifyPending.remove(verifyId);
            const quint64 verifyToken = ++m_tokenCounter;
            m_regionOcrToken.insert(verifyId, verifyToken);
            m_regionOcrInfo.insert(verifyId, {regionFrame.pixelRect, regionFrame.sourceFrameSize});
            m_regionImages.insert(verifyId, regionColorSample(regionFrame.image));
            m_regionOcrHash.insert(verifyId, regionHash);
            m_regionOcrContentHash.insert(verifyId, contentHash);
            m_regionOcrAtMs.insert(verifyId, now);
            m_regionServedProfile.insert(verifyId,
                                         grayProfile(regionFrame.image, regionFrame.image.rect(), 8,
                                                     /*withGradients=*/true));
            m_regionServedHash.insert(verifyId, regionHash);
            m_regionFrameProfile.insert(verifyId,
                                        grayProfile(regionFrame.image, regionFrame.image.rect(),
                                                    8, /*withGradients=*/true));
            qInfo().noquote() << "[OCR] verify region=" << verifyId
                              << "sinceChange=" << (now - m_regionLastChangeMs.value(verifyId, now))
                              << "ms";
            queueRegionOcr(verifyId, verifyToken, regionHash, regionFrame.image);
            continue;
        }

        // Lên lịch: chỉ OCR khi vùng đã settle, qua cooldown, và tối đa vài lần mỗi tick.
        EZTranslator::OcrScheduleDecision decision =
            m_ocrScheduler.process(regionFrame.regionId, regionFrame.image, now, contentHash);
        const bool initialGameProbe =
            m_settings.gameDialogMode && gameTextRects.isEmpty()
            && !m_regionSourceText.contains(regionFrame.regionId)
            && now - m_regionOcrAtMs.value(regionFrame.regionId, qint64(-1000000)) >= 1000
            && !m_regionOcrMailbox.isActive(regionFrame.regionId);
        if (initialGameProbe)
            decision.run = true;

        bool trackedScroll = false;
        bool columnScrollMatch = false;
        ScrollOffset overlayScroll;
        const GrayProfile current = grayProfile(regionFrame.image, regionFrame.image.rect(),
                                                8, /*withGradients=*/true);
        const auto previousFrame = m_regionFrameProfile.constFind(regionFrame.regionId);
        if (!m_settings.gameDialogMode && m_regionBoxes.contains(regionFrame.regionId)
            && previousFrame != m_regionFrameProfile.constEnd()
            && previousFrame->rows.size() == current.rows.size()
            && previousFrame->cols.size() == current.cols.size()) {
            ScrollTracker& tracker = m_regionOverlayTrackers[regionFrame.regionId];
            const int maxShift = tracker.hasState()
                                     ? qMin(regionFrame.image.height() / 3,
                                            qMax(48, int(std::abs(tracker.predictedStep()) + 36)))
                                     : qMax(24, regionFrame.image.height() / 3);
            ScrollOffset scroll = matchProfile(
                *previousFrame, current, maxShift, 0.92,
                tracker.predictedStep(), tracker.hasState());
            // A wide ROI may contain mostly blank page or a fixed UI column. Its
            // full-width row average can lose the text motion even though the text
            // itself still aligns. Retry on narrow columns where translated text
            // was last seen, using the actual previous capture (not a stale OCR
            // image). Never treat an unverified prediction as a match.
            if ((!scroll.valid || scroll.dy == 0)
                && !previousCaptureImage.isNull()
                && previousCaptureImage.size() == frame.image.size()
                && regionFrame.image.height() >= 64) {
                const auto& visibleBoxes = m_regionBoxes.value(regionFrame.regionId);
                int tried = 0;
                QList<int> triedStripes;
                for (const auto& box : visibleBoxes) {
                    const QRect localBox = box.rect.translated(-regionFrame.pixelRect.topLeft());
                    if (localBox.center().y() < 0
                        || localBox.center().y() >= regionFrame.image.height())
                        continue;
                    const int stripeWidth = qMin(240, regionFrame.image.width());
                    const int stripeLeft = qBound(0, localBox.center().x() - stripeWidth / 2,
                                                  regionFrame.image.width() - stripeWidth);
                    bool duplicate = false;
                    for (int usedLeft : triedStripes)
                        duplicate |= std::abs(usedLeft - stripeLeft) < 60;
                    if (duplicate)
                        continue;
                    triedStripes.append(stripeLeft);
                    const QRect localStripe(stripeLeft, 0, stripeWidth,
                                            regionFrame.image.height());
                    const QRect priorStripe = localStripe.translated(regionFrame.pixelRect.topLeft());
                    if (!previousCaptureImage.rect().contains(priorStripe))
                        continue;
                    const GrayProfile prior = grayProfile(previousCaptureImage, priorStripe, 8,
                                                          /*withGradients=*/true);
                    const GrayProfile next = grayProfile(regionFrame.image, localStripe, 8,
                                                         /*withGradients=*/true);
                    const ScrollOffset candidate = matchProfile(
                        prior, next, qMax(maxShift, regionFrame.image.height() / 3), 0.92,
                        tracker.predictedStep(), tracker.hasState());
                    if (candidate.valid && candidate.dy != 0
                        && std::abs(candidate.dx) <= 2
                        && (scroll.dy == 0 || candidate.score > scroll.score)) {
                        scroll = candidate;
                        columnScrollMatch = true;
                    }
                    if (++tried >= 3)
                        break;
                }
            }
            overlayScroll = scroll;
            const bool measured = scroll.valid && std::abs(scroll.dx) <= 2
                                  && scroll.dy != 0
                                  && (std::abs(scroll.dy) > 2 || scroll.score >= 0.96)
                                  && std::abs(scroll.dy) < regionFrame.image.height() / 3;
            ScrollTracker::Step step;
            if (measured) {
                step = tracker.update(scroll.dyExact, true,
                                      regionFrame.image.height() / 3.0);
                m_regionLastScrollMatchMs.insert(regionFrame.regionId, now);
            } else if (decision.changed && tracker.hasState()
                       && now - m_regionLastScrollMatchMs.value(regionFrame.regionId, -1000000)
                              < 250
                       && std::abs(tracker.predictedStep()) >= 2.0
                       && std::abs(tracker.predictedStep()) <= 64.0) {
                // Bridge only a brief missing frame; OCR will validate the settled image.
                step = tracker.update(0.0, false, regionFrame.image.height() / 3.0);
            } else if (decision.changed) {
                tracker.reset();
            }
            if (step.valid && step.dy != 0) {
                const QList<EZTranslator::OverlayBox> shifted =
                    EZTranslator::shiftOverlayBoxes(m_regionBoxes.value(regionFrame.regionId),
                                                    step.dy, regionFrame.pixelRect);
                m_regionBoxes.insert(regionFrame.regionId, shifted);
                trackedScroll = !shifted.isEmpty();
                if (trackedScroll) {
                    m_regionScrolledOverlay.insert(regionFrame.regionId);
                    m_overlaySuspended = false;
                    updateOverlay(regionFrame.sourceFrameSize);
                }
            }
        } else if (previousFrame != m_regionFrameProfile.constEnd()) {
            m_regionOverlayTrackers.remove(regionFrame.regionId);
            m_regionLastScrollMatchMs.remove(regionFrame.regionId);
        }
        m_regionFrameProfile.insert(regionFrame.regionId, current);

        // Scroll-tracking giu ban dich cu chi la TAM THOI. Hen OCR xac minh sau khi anh dung yen;
        // neu day la page-jump bi khop nham thi se co ban dich moi thay vi dung text cu mai.
        if (trackedScroll && m_overlayMode) {
            m_regionVerifyPending.insert(regionFrame.regionId);
            m_regionLastChangeMs.insert(regionFrame.regionId, now);
            if (m_verifyTimer && !m_verifyTimer->isActive())
                m_verifyTimer->start();
        }

        if (decision.changed) {
            if (now - m_overlayScrollLogAtMs >= 2000) {
                m_overlayScrollLogAtMs = now;
                qInfo().noquote() << "[OVERLAY_SCROLL] region=" << regionFrame.regionId
                                  << "kept=" << trackedScroll
                                  << "dy=" << overlayScroll.dy
                                  << "dx=" << overlayScroll.dx
                                  << "score=" << overlayScroll.score
                                  << "valid=" << overlayScroll.valid
                                  << "column=" << columnScrollMatch
                                  << "boxes=" << m_regionBoxes.value(regionFrame.regionId).size();
            }
            // Nội dung vùng vừa ĐỔI (đang cuộn / đổi màn hình): ẩn overlay NGAY. Chỉ hiện lại khi
            // có BẢN DỊCH của nội dung mới - không hiện bản detect nguồn (cách translator xử lý:
            // tắt khi ảnh nguồn đổi, hiện lại đúng vị trí mới khi neo xong).
            m_regionLastChangeMs.insert(regionFrame.regionId, now);
            m_regionObservedProfile.remove(regionFrame.regionId);
            if (!(m_settings.gameDialogMode && gameTextRects.isEmpty())
                && m_ocrWorker && m_regionOcrMailbox.isActive(regionFrame.regionId)
                && m_regionOcrToken.value(regionFrame.regionId) != 0) {
                m_regionOcrToken.insert(regionFrame.regionId, 0);
                m_ocrWorker->noteRegionRequest(regionFrame.regionId, 0);
            }
            m_regionOcrMailbox.remove(regionFrame.regionId);
            if (!trackedScroll) {
                m_regionOverlayTrackers.remove(regionFrame.regionId);
                m_regionLastScrollMatchMs.remove(regionFrame.regionId);
                m_regionBoxes.remove(regionFrame.regionId);
                m_regionScrolledOverlay.remove(regionFrame.regionId);
                m_pendingSourceBoxes.remove(regionFrame.regionId);
                suspendOverlayForChange(now);
            }
        }

        // NỘI DUNG Y HỆT BẢN ĐÃ PHỤC VỤ (trong ngưỡng "không đổi" của scheduler): KHÔNG làm gì cả —
        // trước đây cache box so hash TUYỆT ĐỐI nên nhiễu 1-4 bit làm trượt mỗi khung ⇒ OCR full +
        // gửi dịch + đặt lại overlay ~17 lần/giây trên trang đứng yên (log thật: ~1045 lượt/phút).
        // NHƯNG hash dHash 8×8 quá THÔ: cuộn 1 dòng có thể chỉ lệch ≤4 bit ⇒ nếu chỉ tin hash thì
        // bỏ qua NHẦM ⇒ overlay giữ text cũ ở vị trí cũ mãi ("thi thoảng sai vị trí"). Vì vậy khi
        // hash trong ngưỡng, so thêm PROFILE 1D (rất rẻ): thấy dịch ≥2px là nội dung ĐÃ đổi thật →
        // ép chạy OCR lại để cập nhật vị trí (không ẩn overlay — dịch nhẹ thì cập nhật tại chỗ).
        bool forceOcr = false;
        if (!decision.changed) {
            const auto served = m_regionServedHash.constFind(regionFrame.regionId);
            if (served != m_regionServedHash.constEnd()
                && EZTranslator::imageHammingDistance(served.value(), regionHash)
                       <= m_ocrScheduler.maxDistance()) {
                const auto servedProfile = m_regionServedProfile.constFind(regionFrame.regionId);
                if (servedProfile == m_regionServedProfile.constEnd()) {
                    continue;
                }
                const QRect regionRect(0, 0, regionFrame.image.width(),
                                       regionFrame.image.height());
                const GrayProfile currentProfile =
                    grayProfile(regionFrame.image, regionRect, 4, /*withGradients=*/true);
                const ScrollOffset offset =
                    matchProfile(*servedProfile, currentProfile, 24, 0.90, 0.0);
                const bool moved = offset.valid
                                   && (std::abs(offset.dx) >= 2 || std::abs(offset.dy) >= 2);
                const auto observed = m_regionObservedProfile.constFind(regionFrame.regionId);
                bool movingNow = observed == m_regionObservedProfile.constEnd();
                if (!movingNow) {
                    const ScrollOffset frameOffset =
                        matchProfile(*observed, currentProfile, 24, 0.90, 0.0);
                    movingNow = frameOffset.valid
                                && (std::abs(frameOffset.dx) >= 2
                                    || std::abs(frameOffset.dy) >= 2);
                }
                m_regionObservedProfile.insert(regionFrame.regionId, currentProfile);
                if (!moved)
                    continue; // đứng yên thật → không làm gì (trang tĩnh: 0% CPU/GPU)
                if (movingNow) {
                    m_regionLastChangeMs.insert(regionFrame.regionId, now);
                    if (m_ocrWorker && m_regionOcrMailbox.isActive(regionFrame.regionId)
                        && m_regionOcrToken.value(regionFrame.regionId) != 0) {
                        m_regionOcrToken.insert(regionFrame.regionId, 0);
                        m_ocrWorker->noteRegionRequest(regionFrame.regionId, 0);
                    }
                    m_regionOcrMailbox.remove(regionFrame.regionId);
                    if (!trackedScroll) {
                        m_regionOverlayTrackers.remove(regionFrame.regionId);
                        m_regionLastScrollMatchMs.remove(regionFrame.regionId);
                        m_regionBoxes.remove(regionFrame.regionId);
                        m_regionScrolledOverlay.remove(regionFrame.regionId);
                        m_pendingSourceBoxes.remove(regionFrame.regionId);
                        suspendOverlayForChange(now);
                    }
                }
                forceOcr = true;
            }
        }

        // (Đã BỎ đường OCR TĂNG DẦN khỏi tick: box xấp xỉ của nó KHÔNG bao giờ được hiện — overlay
        // ẩn khi nguồn đổi rồi hiện khi có box chính xác — nên nó chỉ còn hai tác dụng phụ đều có
        // hại: (a) OCR dải bằng BlockingQueuedConnection chặn luồng UI ~20-50ms mỗi khung lúc cuộn;
        // (b) gửi yêu cầu dịch mỗi khung làm hàng đợi worker dịch nghẽn ~200ms (log thật: "dịch"
        // của lượt cache-hit cũng 200ms). Bộ máy dịch giờ: cache dịch theo CÂU + 2 phát emit.)

        // CỬA SỔ CHUYỂN ĐỘNG: ảnh còn đổi trong motion window thì không chạy OCR full
        // (chặn churn giữa lúc cuộn). Máy yếu dùng cửa sổ dài hơn để giảm tải.
        // GameDialog vẫn được probe định kỳ khi chưa có box chữ, kể cả nền đang chuyển động.
        if (!initialGameProbe
            && now - m_regionLastChangeMs.value(regionFrame.regionId, qint64(-1000000))
                   < m_motionWindowMs)
            continue;

        // CACHE BOX THEO NỘI DUNG: nội dung này đã từng OCR thì dùng lại box ngay - KHÔNG OCR lại.
        // So hash theo NGƯỠNG nhiễu (không đòi khớp tuyệt đối): nhiễu capture 1-4 bit từng làm
        // cache trượt mỗi khung dù mắt thường thấy y hệt. Cuộn qua lại vùng đã xem là tức thì
        // (bài học OcrRegionCache của bản translator).
        // forceOcr (nội dung dịch nhẹ nhưng THẬT): KHÔNG dùng cache box — cache so hash thô có thể
        // trả box của nội dung CŨ (lệch ≤4 bit) ⇒ vị trí vẫn sai; phải OCR lại chính ảnh hiện tại.
        const auto cachedRegion = (forceOcr || m_settings.gameDialogMode)
                                      ? m_ocrBoxCache.constEnd()
                                      : m_ocrBoxCache.constFind(regionFrame.regionId);
        if (cachedRegion != m_ocrBoxCache.constEnd()) {
            const QList<EZTranslator::OcrTextBox>* hitBoxes = nullptr;
            int bestDistance = m_ocrScheduler.maxDistance() + 1;
            for (auto it = cachedRegion->constBegin(); it != cachedRegion->constEnd(); ++it) {
                const int distance = EZTranslator::imageHammingDistance(it.key(), regionHash);
                if (distance < bestDistance) {
                    bestDistance = distance;
                    hitBoxes = &it.value();
                }
            }
            if (hitBoxes != nullptr && !hitBoxes->isEmpty()) {
                m_ocrScheduler.noteServedFromCache(regionFrame.regionId, now);
                m_regionServedHash.insert(regionFrame.regionId, regionHash);
                m_regionOcrToken.insert(regionFrame.regionId, ++m_tokenCounter);
                m_regionOcrInfo.insert(regionFrame.regionId,
                                       {regionFrame.pixelRect, regionFrame.sourceFrameSize});
                m_regionImages.insert(regionFrame.regionId, regionColorSample(regionFrame.image));
                m_regionOcrHash.insert(regionFrame.regionId, regionHash);
                m_regionOcrContentHash.insert(regionFrame.regionId, contentHash);
                m_regionServedContentHash.insert(regionFrame.regionId, contentHash);
                m_regionOcrAtMs.insert(regionFrame.regionId, now);
                m_regionServedProfile.insert(
                    regionFrame.regionId,
                    grayProfile(regionFrame.image, regionFrame.image.rect(), 8,
                                /*withGradients=*/true));
                onRegionRecognized(regionFrame.regionId,
                                   m_regionOcrToken.value(regionFrame.regionId), regionHash,
                                   *hitBoxes);
                continue;
            }
        }

        if (!decision.run)
            continue; // deferred → giữ box cũ để overlay không nhấp nháy

        // Gửi OCR sang WORKER THREAD (không chặn UI); kết quả về ở onRegionRecognized.
        m_regionOcrToken.insert(regionFrame.regionId, ++m_tokenCounter);
        m_regionOcrInfo.insert(regionFrame.regionId,
                               {regionFrame.pixelRect, regionFrame.sourceFrameSize});
        // Giữ ảnh ROI đã gửi OCR: plate smart-cover sẽ được dựng từ ĐÚNG ảnh này, nên nó luôn khớp
        // với các box mà worker trả về (không bao giờ dán nền cũ lên nội dung mới).
        m_regionImages.insert(regionFrame.regionId, regionColorSample(regionFrame.image));
        m_regionOcrHash.insert(regionFrame.regionId, regionHash);
        m_regionOcrContentHash.insert(regionFrame.regionId, contentHash);
        m_regionOcrAtMs.insert(regionFrame.regionId, now);
        m_regionServedProfile.insert(
            regionFrame.regionId,
            grayProfile(regionFrame.image, regionFrame.image.rect(), 8,
                        /*withGradients=*/true));
        // Đánh dấu bản đang phục vụ: các khung sau chỉ nhiễu ≤ maxDistance sẽ bị bỏ qua ở đầu tick.
        m_regionServedHash.insert(regionFrame.regionId, regionHash);
        queueRegionOcr(regionFrame.regionId,
                       m_regionOcrToken.value(regionFrame.regionId), regionHash,
                       regionFrame.image);
    }

    m_lastFrameSize = frameSize;
    updateOverlay(frameSize);
}

void AppController::queueRegionOcr(const QString& regionId, quint64 requestToken,
                                   quint64 imageHash, const QImage& image)
{
    if (!m_ocrWorker || !m_ocrReady.load())
        return;

    m_regionVerifyPending.remove(regionId);

    const EZTranslator::OcrRegionMailbox::SubmitResult result = m_regionOcrMailbox.submit(
        regionId, EZTranslator::OcrRegionMailbox::Request{
                      image, requestToken, imageHash,
                      m_settings.novelMode || m_regionScrollReuseEligible.contains(regionId)});
    m_ocrWorker->noteRegionRequest(regionId, requestToken);
    if (result == EZTranslator::OcrRegionMailbox::SubmitResult::Coalesced) {
        qInfo() << "[OCR_METRIC] path=region action=coalesced region=" << regionId
                << "request=" << requestToken;
    }
    dispatchNextRegionOcr();
}

void AppController::dispatchNextRegionOcr()
{
    if (!m_ocrWorker || !m_ocrReady.load())
        return;

    while (const auto dispatch = m_regionOcrMailbox.startNext()) {
        const QString& regionId = dispatch->regionId;
        const EZTranslator::OcrRegionMailbox::Request& request = dispatch->request;

        // Region có thể đã bị xoá hoặc đổi nội dung trong lúc chờ round-robin.
        if (m_regionOcrToken.value(regionId, 0) != request.requestToken
            || m_regionOcrHash.value(regionId, 0) != request.imageHash) {
            m_regionOcrMailbox.finish(regionId);
            continue;
        }

        m_ocrWorker->noteRequestQueued();
        const qint64 queuedAtMs = EZTranslator::ocrMonotonicMs();
        const bool queued = QMetaObject::invokeMethod(
            m_ocrWorker, "recognizeRegion", Qt::QueuedConnection,
            Q_ARG(QString, regionId), Q_ARG(quint64, request.requestToken),
            Q_ARG(quint64, request.imageHash), Q_ARG(qint64, queuedAtMs),
            Q_ARG(QImage, request.image), Q_ARG(bool, request.scrollReuse));
        if (queued)
            return;

        m_regionOcrMailbox.finish(regionId);
        qWarning() << "[OCR] Không xếp được region request:" << regionId;
    }
}

void AppController::onRegionRecognizedPartial(const QString& regionId, quint64 requestToken,
                                              quint64 imageHash,
                                              const QList<EZTranslator::OcrTextBox>& boxes)
{
    if (m_regionOcrToken.value(regionId, 0) != requestToken
        || m_regionOcrHash.value(regionId, 0) != imageHash) {
        qInfo() << "[OCR] Bỏ kết quả partial cũ" << regionId << requestToken << imageHash;
        return;
    }

    // Preview: hiện/dịch cụm dòng đầu ngay, nhưng KHÔNG ghim line pitch, KHÔNG
    // cache box và KHÔNG dựng plate. Kết quả full ngay sau đó sẽ thay thế.
    handleRegionBoxes(regionId, boxes, /*buildPlate=*/false, /*approximate=*/false,
                      /*preview=*/true);
}

void AppController::onRegionRecognized(const QString& regionId, quint64 requestToken,
                                       quint64 imageHash,
                                       const QList<EZTranslator::OcrTextBox>& boxes)
{
    if (m_regionOcrToken.value(regionId, 0) != requestToken
        || m_regionOcrHash.value(regionId, 0) != imageHash) {
        qInfo() << "[OCR] Bỏ kết quả region cũ" << regionId << requestToken << imageHash;
        return;
    }
    const qint64 dispatchMs = m_regionOcrAtMs.value(regionId, 0);
    if (m_settings.gameDialogMode && m_regionSourceText.value(regionId).isEmpty()
        && m_regionLastChangeMs.value(regionId, -1) > dispatchMs) {
        // Lượt dò ban đầu có thể chụp đúng hộp thoại nhưng nền vẫn động. Chỉ giữ
        // geometry làm mask nhận diện cho frame sau; không dịch hay vẽ text stale.
        const QRect regionRect = m_regionOcrInfo.value(regionId).pixelRect;
        QList<QRect> lines;
        QStringList texts;
        for (const EZTranslator::OcrTextBox& box : boxes) {
            if (box.text.trimmed().isEmpty())
                continue;
            lines.append(box.rect.translated(regionRect.topLeft()));
            texts.append(box.text.simplified());
        }
        if (!lines.isEmpty()) {
            m_regionLines.insert(regionId, lines);
            m_regionLineTexts.insert(regionId, texts);
            qInfo() << "[GAME_DIALOG] initial text boxes harvested from moving frame region="
                    << regionId << "boxes=" << lines.size();
        }
        return;
    }
    quint64 servedContentHash = m_regionOcrContentHash.value(regionId, 0);
    if (m_settings.gameDialogMode && !boxes.isEmpty()) {
        const RegionFrameInfo info = m_regionOcrInfo.value(regionId);
        if (!m_lastCapture.image.isNull() && m_lastCapture.image.rect().contains(info.pixelRect)) {
            const QImage currentRegion = m_lastCapture.image.copy(info.pixelRect);
            QList<QRect> currentTextRects;
            currentTextRects.reserve(boxes.size());
            for (const EZTranslator::OcrTextBox& box : boxes)
                currentTextRects.append(box.rect);
            servedContentHash = EZTranslator::imageTextDifferenceHash(currentRegion, currentTextRects);
        }
    }
    m_regionServedContentHash.insert(regionId, servedContentHash);
    handleRegionBoxes(regionId, boxes, /*buildPlate=*/true);
}

void AppController::onRegionScrollMatched(const QString& regionId, quint64 requestToken,
                                          quint64 imageHash, int dy, double score)
{
    if (m_regionOcrToken.value(regionId, 0) != requestToken
        || m_regionOcrHash.value(regionId, 0) != imageHash
        || m_regionScrolledOverlay.contains(regionId))
        return;

    const auto existing = m_regionBoxes.constFind(regionId);
    const QRect bounds = m_regionOcrInfo.value(regionId).pixelRect;
    if (existing == m_regionBoxes.constEnd() || existing->isEmpty() || !bounds.isValid())
        return;

    const auto shifted = EZTranslator::shiftOverlayBoxes(*existing, dy, bounds);
    if (shifted.isEmpty())
        return;
    m_regionBoxes.insert(regionId, shifted);
    m_regionScrolledOverlay.insert(regionId);
    m_regionDisplayProfile.remove(regionId);
    m_overlaySuspended = false;
    qInfo().noquote() << "[OVERLAY_SCROLL] OCR-confirmed region=" << regionId
                      << "dy=" << dy << "score=" << score << "boxes=" << shifted.size();
    updateOverlay(m_lastFrameSize);
}

void AppController::onRegionOcrFinished(const QString& regionId, quint64 requestToken)
{
    if (!m_regionOcrMailbox.isActive(regionId))
        return;

    qInfo() << "[OCR] region finished" << regionId << "request" << requestToken;
    m_regionOcrMailbox.finish(regionId);
    dispatchNextRegionOcr();
}

void AppController::onRegionPlateReady(const QString& regionId, quint64 requestToken,
                                       quint64 imageHash, const QImage& plate)
{
    if (plate.isNull())
        return;

    if (m_regionOcrToken.value(regionId, 0) != requestToken
        || m_regionOcrHash.value(regionId, 0) != imageHash) {
        qInfo() << "[OCR] Bỏ plate region cũ" << regionId << requestToken << imageHash;
        return;
    }

    QHash<quint64, QImage>& pool = m_regionPlates[regionId];
    if (pool.size() >= 3)
        pool.clear(); // bounded: giữ vài nội dung mới nhất (cuộn qua lại vẫn trúng)
    pool.insert(imageHash, plate);

    // Gioi han tong RAM plate tren MOI vung, khong chi tung vung.
    qint64 totalPlateBytes = 0;
    for (auto regionIt = m_regionPlates.constBegin(); regionIt != m_regionPlates.constEnd();
         ++regionIt) {
        for (const QImage& image : regionIt.value())
            totalPlateBytes += image.sizeInBytes();
    }
    if (totalPlateBytes > kPlateCacheBudgetBytes) {
        // Giu plate vua tao; bo phan con lai de RAM khong phinh.
        m_regionPlates.clear();
        m_regionPlates[regionId].insert(imageHash, plate);
        // Box/overlay cu co the dang giu reference toi plate cu -> xoa luon.
        m_regionBoxes.clear();
        m_regionDisplayProfile.clear();
        m_regionFrameProfile.clear();
        m_regionScrolledOverlay.clear();
        if (m_textOverlay)
            m_textOverlay->clear();
    }

    // Overlay đang hiện nội dung này mà box chưa có plate (đang che tạm bằng màu đặc) thì dán
    // plate vào và vẽ lại — nâng cấp nền "màu đặc" → "nền đã xoá chữ", không chờ thêm gì.
    const auto boxesIt = m_regionBoxes.find(regionId);
    if (boxesIt == m_regionBoxes.end() || boxesIt->isEmpty()
        || m_regionScrolledOverlay.contains(regionId))
        return;
    const quint64 current = m_regionOcrHash.value(regionId, 0);
    if (current == 0
        || EZTranslator::imageHammingDistance(current, imageHash) > m_ocrScheduler.maxDistance())
        return;
    const QRect regionBounds = m_regionOcrInfo.contains(regionId)
                                   ? m_regionOcrInfo.value(regionId).pixelRect
                                   : QRect();
    if (!regionBounds.isValid())
        return;
    const QColor textColor = contrastTextColor(m_regionImages.value(regionId));
    const QColor coverColor = regionCoverColor(m_regionImages.value(regionId));
    for (EZTranslator::OverlayBox& overlay : boxesIt.value()) {
        overlay.plate = plate;
        overlay.plateRect = regionBounds;
        overlay.textColor = textColor;
        overlay.coverColor = coverColor;
    }
    if (m_floatingMode)
        refreshFloatingWindow();
    updateOverlay(m_lastFrameSize);
}

QImage AppController::findRegionPlate(const QString& regionId, quint64 imageHash) const
{
    if (imageHash == 0)
        return {};
    const auto region = m_regionPlates.constFind(regionId);
    if (region == m_regionPlates.constEnd())
        return {};
    QImage best;
    int bestDistance = m_ocrScheduler.maxDistance() + 1;
    for (auto it = region->constBegin(); it != region->constEnd(); ++it) {
        const int distance = EZTranslator::imageHammingDistance(it.key(), imageHash);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = it.value();
        }
    }
    return best;
}

void AppController::handleRegionBoxes(const QString& regionId,
                                      const QList<EZTranslator::OcrTextBox>& boxesIn,
                                      bool buildPlate, bool approximate, bool preview)
{
    // BẢN SAO ngay đầu hàm: đường cache-hit truyền vào CHÍNH list đang nằm trong m_ocrBoxCache,
    // mà cuối hàm này lại insert/clear cache đó — rehash/clear giải phóng node và mọi tham chiếu
    // tới `boxesIn` thành dangling (use-after-free → crash 0xc0000005 trong Qt6Core sau một lúc
    // dùng overlay, khi cache đủ 8 nội dung và bị clear).
    const QList<EZTranslator::OcrTextBox> boxes = boxesIn;

    // Preview của full OCR CPU không phải kết quả cuối: tuyệt đối không cache box
    // dưới hash của cả ảnh, không dựng plate và không ghim line pitch/paragraph.
    if (preview)
        buildPlate = false;

    const auto info = m_regionOcrInfo.constFind(regionId);
    if (info == m_regionOcrInfo.constEnd())
        return;
    if (boxes.isEmpty())
        return; // không đọc được gì → giữ kết quả trước, không xóa overlay

    const QRect pixelRect = info->pixelRect;
    const qint64 ocrMs = m_clock.isValid() ? m_clock.elapsed() - m_regionOcrAtMs.value(regionId, 0)
                                           : -1;

    // A Chinese/Japanese novel can have short lines (<= 40 chars), so the old
    // UI heuristic would otherwise route it to per-box layout before the novel
    // branch gets a chance. Run a cheap layout probe first.
    bool novelCandidate = false;
    {
        const auto quick = EZTranslator::OcrTextAssembler::assembleDocument(boxes, 0, 0.0);
        QList<QRect> quickRects;
        QStringList quickTexts;
        for (const auto& line : quick.lines) {
            quickRects.append(line.rect.translated(pixelRect.topLeft()));
            quickTexts.append(line.text.simplified());
        }
        novelCandidate =
            EZTranslator::OverlayLayout::looksLikeNovel(quickRects, quickTexts, pixelRect,
                                                        m_settings.novelMode);
    }

    // UI layout: moi OCR box (label/value/button) la mot don vi dich va mot box
    // overlay rieng. Tranh group paragraph lam label+value bi gop thanh mot cau
    // roi dat lech sang control ben canh.
    if (!novelCandidate && useUiBoxLayout(boxes)) {
        QList<EZTranslator::OcrTextBox> rawBoxes;
        QStringList texts;
        QList<QRect> rects;
        rawBoxes.reserve(boxes.size());
        texts.reserve(boxes.size());
        rects.reserve(boxes.size());
        for (const EZTranslator::OcrTextBox& box : boxes) {
            const QString text = compactCjkSpaces(box.text.simplified());
            rawBoxes.append(box);
            rects.append(box.rect.translated(pixelRect.topLeft()));
            if (!text.isEmpty())
                texts.append(text);
        }

        if (rawBoxes.size() >= 2) {
            m_regionUiBoxes.insert(regionId, rawBoxes);
            m_regionLines.insert(regionId, rects);
            m_regionLineTexts.insert(regionId, texts);
            m_regionSourceText.insert(regionId, texts.join(QStringLiteral("\n\n")));
            if (m_floatingMode)
                refreshFloatingWindow();
            m_regionStateAtMs.insert(
                regionId, m_regionOcrAtMs.value(regionId, m_clock.isValid()
                                                               ? m_clock.elapsed()
                                                               : 0));

            if (buildPlate) {
                const quint64 hash = m_regionOcrHash.value(regionId);
                if (hash != 0) {
                    QHash<quint64, QList<EZTranslator::OcrTextBox>>& regionCache =
                        m_ocrBoxCache[regionId];
                    if (regionCache.size() >= m_resourcePlan.ocrBoxCachePerRegion)
                        regionCache.clear();
                    regionCache.insert(hash, boxes);
                }
            }

            qInfo().noquote() << "[OCR]" << regionId << "->" << rawBoxes.size()
                              << "UI box(es), sau" << ocrMs << "ms";
            m_regionNovelParagraphs.remove(regionId);
            requestRegionTranslation(regionId, m_regionSourceText.value(regionId));
            return;
        }
    }
    m_regionUiBoxes.remove(regionId);
    qDebug().noquote() << "[OCR]" << regionId
                       << (approximate ? QStringLiteral("[tăng dần]") : QString()) << "->"
                       << boxes.size() << "line(s), sau" << ocrMs << "ms";

    // Ghim nhịp dòng + khe đoạn cho vùng này (hysteresis kiểu bản gốc): đo trên frame
    // hiện tại nhưng chỉ cập nhật khi lệch đáng kể. Nhờ vậy cấu trúc đoạn không nhấp
    // nháy khi box rung, và cache dịch không bị trượt mỗi frame.
    // Box XẤP XỈ (đường tăng dần) không được ghim: ước lượng cuộn làm nhịp đo sai, ghim vào là
    // đầu độc cả những lượt CHÍNH XÁC sau (assembly chia dòng khác đi ⇒ vị trí đoạn lệch).
    // Preview (cụm dòng đầu của CPU progressive) cũng không được ghim vì mới thấy một phần trang.
    if (!approximate && !preview) {
        const int measuredPitch = EZTranslator::OcrTextAssembler::estimateLinePitch(boxes);
        if (measuredPitch > 0) {
            const int previous = m_regionLinePitch.value(regionId, 0);
            if (previous <= 0 || qAbs(measuredPitch - previous) > qMax(2, previous / 3))
                m_regionLinePitch.insert(regionId, measuredPitch);
        }
        const double measuredRatio = EZTranslator::OcrTextAssembler::estimateParagraphRatio(boxes);
        if (measuredRatio > 0.0) {
            const double previous = m_regionParagraphRatio.value(regionId, 0.0);
            if (previous <= 0.0 || qAbs(measuredRatio - previous) > 0.2)
                m_regionParagraphRatio.insert(regionId, measuredRatio);
        }
    }

    // Ghép các dòng thành văn bản + rect từng dòng (ROI → toạ độ frame) bằng nhịp/khe ĐÃ GHIM.
    const EZTranslator::OcrTextAssembler::Assembled assembled =
        EZTranslator::OcrTextAssembler::assembleDocument(
            boxes, m_regionLinePitch.value(regionId, 0),
            m_regionParagraphRatio.value(regionId, 0.0));

    // Kết quả OCR của khung ĐÃ BỊ ĐỔI TIẾP trong lúc worker chạy (lúc dispatch cũ hơn lần đổi mới
    // nhất): box vẫn đúng cho ảnh đã OCR nên vẫn vào cache nội dung, nhưng KHÔNG được cập nhật
    // trạng thái hiển thị / gửi dịch — nội dung đó đã trôi qua, áp vào là hiện box cũ lên nội dung
    // mới (đúng lỗi "sai vị trí rồi tự sửa"). Lượt OCR của nội dung mới sẽ tự chạy.
    {
        const qint64 dispatchMs =
            m_regionOcrAtMs.value(regionId, m_clock.isValid() ? m_clock.elapsed() : 0);
        if (m_regionLastChangeMs.value(regionId, -1) > dispatchMs) {
            if (buildPlate) {
                const quint64 staleHash = m_regionOcrHash.value(regionId);
                if (staleHash != 0) {
                    QHash<quint64, QList<EZTranslator::OcrTextBox>>& regionCache =
                        m_ocrBoxCache[regionId];
                    if (regionCache.size() >= m_resourcePlan.ocrBoxCachePerRegion)
                        regionCache.clear();
                    regionCache.insert(staleHash, boxes);
                }
            }
            return;
        }
    }

    // ĐƯỜNG TĂNG DẦN: box là XẤP XỈ (dịch theo ước lượng cuộn) nên CHỈ gửi yêu cầu dịch để HÂM
    // NÓNG cache dịch cho lượt CHÍNH XÁC sắp tới, rồi thoát — không cập nhật dòng/nguồn/pending,
    // không vẽ lên overlay. Trước đây box xấp xỉ được hiện ngay (trúng cache dịch nên rất nhanh)
    // rồi lượt chính xác về sau đặt lại — đúng cảnh "sai vị trí rồi tự sửa" người dùng vừa báo.
    // Hiển thị giờ CHỈ đến từ đường chính xác (full OCR / cache nội dung) ⇒ đúng vị trí ngay lần đầu.
    if (approximate) {
        requestRegionTranslation(regionId, assembled.text);
        return;
    }

    QList<QRect> lineRects;
    QStringList lineTexts;
    lineRects.reserve(assembled.lines.size());
    lineTexts.reserve(assembled.lines.size());
    for (const EZTranslator::OcrTextAssembler::LineText& line : assembled.lines) {
        lineRects.append(line.rect.translated(pixelRect.topLeft()));
        lineTexts.append(line.text.simplified());
    }
    m_regionLines.insert(regionId, lineRects);
    m_regionLineTexts.insert(regionId, lineTexts);
    maybeUpdateDetectedSource(assembled.text);

    // Novel/reading layout: one paragraph block per source paragraph, translated
    // with paragraph context. Deliberately separate from UI box layout and from
    // the old line-count paragraph anchor.
    if (novelCandidate
        || EZTranslator::OverlayLayout::looksLikeNovel(lineRects, lineTexts, pixelRect,
                                                       m_settings.novelMode)) {
        QList<EZTranslator::OverlayLayout::NovelParagraph> paragraphs =
            EZTranslator::OverlayLayout::groupNovelParagraphs(lineRects, lineTexts);
        if (paragraphs.size() >= 2) {
            QStringList paragraphTexts;
            paragraphTexts.reserve(paragraphs.size());
            for (const auto& paragraph : paragraphs)
                paragraphTexts.append(paragraph.text.trimmed());
            const QString novelText = paragraphTexts.join(QStringLiteral("\n\n"));
            if (!novelText.trimmed().isEmpty()) {
                m_regionNovelParagraphs.insert(regionId, paragraphs);
                if (!preview)
                    m_regionScrollReuseEligible.insert(regionId);
                m_regionSourceText.insert(regionId, novelText);
                if (m_floatingMode)
                    refreshFloatingWindow();
                m_regionStateAtMs.insert(
                    regionId, m_regionOcrAtMs.value(regionId, m_clock.isValid()
                                                                   ? m_clock.elapsed()
                                                                   : 0));
                // OCR has now located the settled paragraphs. Re-anchor translations
                // already seen on an earlier scroll immediately; leave only newly
                // exposed paragraphs for the translation worker.
                EZTranslator::TranslatedDocument cachedDoc;
                cachedDoc.paragraphs.reserve(paragraphs.size());
                bool hasCachedParagraph = false;
                for (const auto& paragraph : paragraphs) {
                    const QString cached =
                        m_paragraphTranslations.value(paragraphCacheKey(paragraph.text));
                    cachedDoc.paragraphs.append(cached);
                    hasCachedParagraph |= !cached.trimmed().isEmpty();
                }
                if (m_regionScrolledOverlay.contains(regionId)) {
                    if (hasCachedParagraph) {
                        auto cachedBoxes = EZTranslator::OverlayLayout::buildNovelBoxes(
                            cachedDoc, paragraphs, pixelRect);
                        const QImage regionImage = m_regionImages.value(regionId);
                        const QImage plate =
                            findRegionPlate(regionId, m_regionOcrHash.value(regionId));
                        for (auto& overlay : cachedBoxes) {
                            overlay.textColor = contrastTextColor(regionImage);
                            overlay.coverColor = regionPaperColor(regionImage);
                            if (!plate.isNull()) {
                                overlay.plate = plate;
                                overlay.plateRect = pixelRect;
                            }
                        }
                        m_regionBoxes.insert(regionId, cachedBoxes);
                    } else {
                        m_regionBoxes.remove(regionId);
                    }
                    m_regionScrolledOverlay.remove(regionId);
                    m_overlaySuspended = false;
                    updateOverlay(m_lastFrameSize);
                }
                if (buildPlate) {
                    const quint64 hash = m_regionOcrHash.value(regionId);
                    if (hash != 0) {
                        QHash<quint64, QList<EZTranslator::OcrTextBox>>& regionCache =
                            m_ocrBoxCache[regionId];
                        if (regionCache.size() >= m_resourcePlan.ocrBoxCachePerRegion)
                            regionCache.clear();
                        regionCache.insert(hash, boxes);
                    }
                }
                qInfo().noquote() << "[OCR]" << regionId << "->" << paragraphs.size()
                                  << "novel paragraph(s), sau" << ocrMs << "ms";
                requestRegionTranslation(regionId, novelText);
                return;
            }
        }
    }
    m_regionNovelParagraphs.remove(regionId);

    // Hiện NGAY bản nguồn (chưa dịch) để overlay không trống; bản dịch thay sau.
    const EZTranslator::TranslatedDocument sourceDoc =
        EZTranslator::DocumentTranslator(nullptr)
            .translate(assembled.text, m_settings.sourceLanguage,
                       m_settings.targetLanguage, nullptr);

    QList<EZTranslator::OverlayBox> regionBoxes =
        buildParagraphBoxes(sourceDoc, lineRects, lineTexts, pixelRect);
    if (regionBoxes.isEmpty()) {
        // Không có cấu trúc đoạn → hiện từng dòng OCR.
        for (const EZTranslator::OcrTextBox& box : boxes) {
            if (box.text.isEmpty())
                continue;
            EZTranslator::OverlayBox overlay;
            overlay.rect = box.rect.translated(pixelRect.topLeft());
            overlay.coverRects.append(overlay.rect);
            overlay.text = box.text;
            overlay.fontPixelSize = qMax(9, int(box.rect.height() * 0.65));
            regionBoxes.append(overlay);
        }
    }
    m_regionSourceText.insert(regionId, assembled.text);
    if (m_floatingMode)
        refreshFloatingWindow();
    // Mốc thời gian của trạng thái CHÍNH XÁC này: lấy thời điểm DISPATCH OCR (không phải lúc kết
    // quả về) — kết quả dịch chỉ được áp khi không có lần đổi nội dung nào mới hơn mốc này (xem
    // onTranslationReady), tránh hiện box cũ lên nội dung vừa cuộn.
    m_regionStateAtMs.insert(regionId, m_regionOcrAtMs.value(regionId, m_clock.isValid() ? m_clock.elapsed() : 0));

    // Lưu box vào CACHE NỘI DUNG (hash của đúng ảnh đã OCR ra chúng): lần sau gặp lại nội dung này
    // là dùng ngay, không OCR lại — cuộn qua lại vùng đã xem là tức thì. CHỈ lưu box CHÍNH XÁC
    // (buildPlate=true): box từ đường tăng dần là xấp xỉ (dịch theo ước lượng), lưu vào cache sẽ
    // làm cache trả box lệch nhẹ về sau.
    if (buildPlate) {
        const quint64 hash = m_regionOcrHash.value(regionId);
        if (hash != 0) {
            QHash<quint64, QList<EZTranslator::OcrTextBox>>& regionCache = m_ocrBoxCache[regionId];
            if (regionCache.size() >= m_resourcePlan.ocrBoxCachePerRegion)
                regionCache.clear(); // bounded: giữ các nội dung mới nhất
            regionCache.insert(hash, boxes);
        }
    }

    // Smart cover cho LƯỚI AN TOÀN (chỉ hiện ở fallback 8s khi engine dịch hỏng hẳn): dán plate đã
    // dựng SẴN trên luồng OCR (tra theo hash nội dung) nếu có; không có thì box tự phủ nền bằng
    // coverRects — không dựng gì trên luồng UI.
    if (buildPlate) {
        const QImage plate = findRegionPlate(regionId, m_regionOcrHash.value(regionId, 0));
        const QColor textColor = contrastTextColor(m_regionImages.value(regionId));
        const QColor coverColor = regionCoverColor(m_regionImages.value(regionId));
        for (EZTranslator::OverlayBox& overlay : regionBoxes) {
            overlay.textColor = textColor;
            overlay.coverColor = coverColor;
            if (!plate.isNull()) {
                overlay.plate = plate;
                overlay.plateRect = pixelRect;
            }
        }
    }

    // Tự nhận diện ngôn ngữ nguồn — chạy CẢ KHI người dùng đặt cứng ngôn ngữ: nội dung có thể
    // KHÁC HỆ CHỮ (đặt "en" cho trang truyện rồi mở cửa sổ tiếng Trung: model en đọc ra rác, bản
    // dịch bị lọc bỏ ⇒ "không phản ứng gì" — đúng ca image7/WeGame). Chỉ đổi khi KHÁC HỆ CHỮ
    // (Latin ↔ CJK) và ngôn ngữ đó app có model — tránh nhảy cấu hình lung tung theo nhiễu.
    {
        const QString detected = EZTranslator::LanguageDetect::detect(assembled.text);
        const bool autoSource = EZTranslator::LanguageDetect::isAuto(m_settings.sourceLanguage);
        const QString configured = autoSource ? m_detectedSource : m_settings.sourceLanguage;
        const bool supported = detected == QLatin1String("en") || detected == QLatin1String("zh")
                               || detected == QLatin1String("ja");
        const auto family = [](const QString& code) {
            if (code == QLatin1String("zh") || code == QLatin1String("ja"))
                return QStringLiteral("cjk");
            if (code == QLatin1String("en"))
                return QStringLiteral("latin");
            return QString();
        };
        if (!detected.isEmpty() && detected != m_detectedSource && supported
            && family(detected) != family(configured)) {
            m_detectedSource = detected;
            if (m_translateWorker) {
                m_translationConfig = translationConfig(detected);
                QMetaObject::invokeMethod(m_translateWorker, "configure", Qt::QueuedConnection,
                                          Q_ARG(EZTranslator::TranslationConfig,
                                                m_translationConfig));
            }
            m_mainWindow->setStatus(QStringLiteral("Ngôn ngữ nguồn: ") + detected,
                                    QColor("#38bdf8"));
            qInfo() << "[Translate] nguồn nhận diện KHÁC hệ chữ cấu hình → đổi cặp sang" << detected;
        }
    }

    // Yêu cầu dịch BẤT ĐỒNG BỘ NGAY (trước khi dựng plate): worker dịch chạy song song với phần
    // dựng plate bên dưới — bớt ~30-80ms trên đường tới lúc bản dịch hiện lên.
    requestRegionTranslation(regionId, assembled.text);

    // Dựng PLATE giờ nằm trên LUỒNG OCR (OcrWorker::recognizeRegion → regionPlateReady): plate
    // 100-270ms dựng trên luồng UI từng chặn mọi kết quả OCR/dịch phía sau (log thật: OCR ghi
    // "sau 663ms"). Luồng UI chỉ tra plate theo hash nội dung khi cần hiện (onTranslationReady).

    // KHÔNG hiện bản nguồn nữa: overlay chỉ hiện BẢN DỊCH (yêu cầu người dùng). Box nguồn giữ lại
    // làm lưới an toàn 8s nếu engine dịch lỗi hẳn.
    if (!regionBoxes.isEmpty())
        m_pendingSourceBoxes.insert(regionId, regionBoxes);

    updateOverlay(m_lastFrameSize);
}

void AppController::maybeUpdateDetectedSource(const QString& text)
{
    const QString detected = EZTranslator::LanguageDetect::detect(text);
    const bool autoSource = EZTranslator::LanguageDetect::isAuto(m_settings.sourceLanguage);
    const QString configured = autoSource ? m_detectedSource : m_settings.sourceLanguage;
    const bool supported = detected == QLatin1String("en") || detected == QLatin1String("zh")
                           || detected == QLatin1String("ja");
    const auto family = [](const QString& code) {
        if (code == QLatin1String("zh") || code == QLatin1String("ja"))
            return QStringLiteral("cjk");
        if (code == QLatin1String("en"))
            return QStringLiteral("latin");
        return QString();
    };
    if (!detected.isEmpty() && detected != m_detectedSource && supported
        && family(detected) != family(configured)) {
        m_detectedSource = detected;
        if (m_translateWorker) {
            m_translationConfig = translationConfig(detected);
            QMetaObject::invokeMethod(m_translateWorker, "configure", Qt::QueuedConnection,
                                      Q_ARG(EZTranslator::TranslationConfig,
                                            m_translationConfig));
        }
        m_mainWindow->setStatus(QStringLiteral("Ngon ngu nguon: ") + detected,
                                QColor("#38bdf8"));
        qInfo() << "[Translate] source language changed to" << detected;
    }
}

void AppController::requestRegionTranslation(const QString& regionId, const QString& sourceText)
{
    // Yêu cầu dịch BẤT ĐỒNG BỘ: worker dịch chạy song song với phần dựng plate / chờ OCR phía sau.
    // Text nguồn của CHÍNH lượt này được nhớ lại để khi kết quả về còn đối chiếu theo NỘI DUNG
    // (neo nội dung — không bỏ theo token, xem onTranslationReady).
    if (!ensureTranslator())
        return;
    m_regionToken.insert(regionId, ++m_tokenCounter);
    const quint64 requestToken = m_regionToken.value(regionId);
    {
        QHash<quint64, QString>& byToken = m_regionRequestSource[regionId];
        if (byToken.size() >= 4)
            byToken.clear(); // bounded: chỉ giữ vài lượt gần nhất
        byToken.insert(requestToken, sourceText);
    }
    m_regionTranslateAtMs.insert(regionId, m_clock.isValid() ? m_clock.elapsed() : 0);
    qInfo().noquote() << "[Translate] gửi vùng" << regionId
                      << "source=" << (m_detectedSource.isEmpty() ? m_settings.sourceLanguage
                                                                  : m_detectedSource)
                      << "target=" << m_settings.targetLanguage << "chars=" << sourceText.size()
                      << "paragraphs=" << sourceText.count(QStringLiteral("\n\n")) + 1;
    QMetaObject::invokeMethod(m_translateWorker, "translate", Qt::QueuedConnection,
                              Q_ARG(QString, regionId), Q_ARG(QString, sourceText),
                              Q_ARG(quint64, requestToken),
                              Q_ARG(qint64, EZTranslator::ocrMonotonicMs()));
}

void AppController::onTranslationReady(const QString& regionId, quint64 token,
                                       const EZTranslator::TranslatedDocument& document)
{
    // Câu warm-up lúc bật dịch (khởi động sẵn sidecar): không đụng gì tới overlay.
    if (regionId == QLatin1String("__warmup__"))
        return;

    // Kết quả cho chế độ "dịch khi chỉ chuột".
    if (regionId == kHoverId) {
        if (token != m_hoverTranslationToken)
            return; // kết quả cũ (đã có yêu cầu hover mới hơn)
        if (!m_hoverPopup || m_overlayMode)
            return; // đang ở chế độ Overlay: không hiện cửa sổ nổi

        m_hoverPending = false;
        const QString translated = document.displayText();
        const qint64 hoverTranslateMs =
            m_clock.isValid() ? m_clock.elapsed() - m_hoverProbeMs : -1;
        qInfo().noquote() << "[PIPE_METRIC] path=hover stage=translation request=" << token
                          << "translationMs=" << hoverTranslateMs
                          << "presented=1 sourceChars=" << m_hoverSource.size()
                          << "paragraphs=" << document.paragraphs.size()
                          << "complete=" << document.complete
                          << "chars=" << translated.size();

        if (translated.isEmpty()) {
            // Không dịch được → vẫn hiện dòng gốc để không im lặng.
            m_hoverPopup->showTranslation(m_hoverSource, QString(),
                                          cursorToLogical(m_hoverLineCursor),
                                          sizeToLogical(m_hoverLineClient.width()),
                                          m_selectionActive ? hoverSelectionLogicalRect() : QRect());
            return;
        }

        m_hoverTranslation = translated;
        if (m_hoverCache.size() >= kHoverCacheLimit)
            m_hoverCache.clear();
        m_hoverCache.insert(m_hoverSource, translated);
        // Hiện ĐỒNG THỜI nguồn + bản dịch, chỉ một lần (không còn hai giai đoạn).
        m_hoverPopup->showTranslation(m_hoverSource, translated,
                                      cursorToLogical(m_hoverLineCursor),
                                      sizeToLogical(m_hoverLineClient.width()),
                                      m_selectionActive ? hoverSelectionLogicalRect() : QRect());
        return;
    }

    // Neo theo NỘI DUNG (không theo token — bỏ theo token từng gây "lúc dịch lúc không"): CHỈ áp
    // bản dịch khi text nguồn của CHÍNH lượt yêu cầu khớp text nguồn đang là trạng thái hiển thị.
    // Token KHÔNG được dùng để miễn kiểm tra: đường OCR TĂNG DẦN cũng gửi yêu cầu (chỉ để hâm
    // nóng cache dịch, không cập nhật dòng hiển thị) — token còn khớp mà áp luôn thì box bị vẽ
    // bằng dòng của nội dung CŨ ⇒ "hiện sai vị trí rồi lượt chính xác mới sửa" (lỗi người dùng báo).
    // Không có text nguồn ghi lại (map bị cắt tỉa) cũng coi như không khớp.
    const QString requestSource = m_regionRequestSource.value(regionId).value(token);
    if (requestSource.isEmpty() || requestSource != m_regionSourceText.value(regionId))
        return; // bản dịch của nội dung cũ; lượt dịch của nội dung mới đang bay sẽ tự áp

    // Trạng thái hiển thị (dòng/nguồn) đã bị lần đổi nội dung MỚI HƠN thay thế kể từ lượt OCR
    // sinh ra bản dịch này (người dùng cuộn tiếp): áp là hiện box cũ lên nội dung mới. Lượt OCR
    // của nội dung mới sẽ tự áp bản dịch của nó.
    if (m_regionLastChangeMs.value(regionId, -1) > m_regionStateAtMs.value(regionId, -1))
        return;

    // Không dựng cover-only overlay khi engine dịch chưa sẵn sàng hoặc trả về rỗng.
    // Trước đây bản cập nhật rỗng vẫn đi qua layout novel, tạo box che toàn bộ chữ gốc
    // nhưng không có text dịch nào để vẽ. Fallback source sẽ xử lý trường hợp engine lỗi.
    bool hasTranslatedText = false;
    for (const QString& paragraph : document.paragraphs) {
        if (!paragraph.trimmed().isEmpty()) {
            hasTranslatedText = true;
            break;
        }
    }
    if (!hasTranslatedText) {
        qWarning().noquote() << "[Translate] bỏ qua overlay rỗng; chưa có bản dịch hợp lệ"
                             << "region=" << regionId << "token=" << token
                             << "complete=" << document.complete;
        return;
    }

    const auto lines = m_regionLines.constFind(regionId);
    if (lines == m_regionLines.constEnd())
        return;

    const QRect regionBounds = m_regionOcrInfo.contains(regionId)
                                   ? m_regionOcrInfo.value(regionId).pixelRect
                                   : QRect();

    QList<EZTranslator::OverlayBox> boxes;
    const auto novelParagraphs = m_regionNovelParagraphs.constFind(regionId);
    const auto uiBoxes = m_regionUiBoxes.constFind(regionId);
    if (novelParagraphs != m_regionNovelParagraphs.constEnd()) {
        boxes = EZTranslator::OverlayLayout::buildNovelBoxes(document, *novelParagraphs,
                                                             regionBounds);
        qInfo().noquote() << "[Translate] novel layout" << regionId << "->" << boxes.size()
                          << "paragraph box(es)";
    } else if (uiBoxes != m_regionUiBoxes.constEnd()) {
        boxes.reserve(uiBoxes->size());
        int textIndex = 0;
        for (int i = 0; i < uiBoxes->size(); ++i) {
            const EZTranslator::OcrTextBox& raw = uiBoxes->at(i);
            const QString rawText = compactCjkSpaces(raw.text.simplified());
            EZTranslator::OverlayBox overlay;
            overlay.rect = raw.rect.translated(regionBounds.topLeft());
            overlay.coverRects.append(overlay.rect);
            overlay.fontPixelSize = qMax(9, int(raw.rect.height() * 0.65));
            if (rawText.isEmpty()) {
                // Detector thay chu nhung OCR khong doc ra text: van dat panel
                // de che chu goc, khong de lo tieng Trung.
                overlay.coverOnly = true;
                boxes.append(overlay);
                continue;
            }
            QString translated = document.paragraphs.value(textIndex++).trimmed();
            if (translated.isEmpty())
                translated = rawText;
            overlay.text = translated;
            boxes.append(overlay);
        }
    } else {
        boxes = buildParagraphBoxes(document, *lines, m_regionLineTexts.value(regionId),
                                    regionBounds, /*skipUntranslated=*/true);
    }
    // Smart cover: chỉ TRA plate đã dựng SẴN trên LUỒNG OCR theo hash nội dung ảnh (xem
    // OcrWorker::recognizeRegion → onRegionPlateReady) rồi dán — KHÔNG dựng ở luồng UI nữa (dựng
    // 100-270ms từng chặn cả dây chuyền: OCR ghi "sau 663ms", lượt trúng cache cũng "dịch 317ms").
    // Chưa có plate (kết quả dịch về trước khi worker dựng xong) thì hiện tạm bằng coverRects màu
    // đặc; plate về sau sẽ được dán vào và vẽ lại (onRegionPlateReady).
    if (!boxes.isEmpty() && regionBounds.isValid()) {
        const QImage regionImage = m_regionImages.value(regionId);
        const QColor textColor = contrastTextColor(regionImage);
        // Màu che TẠM theo nền vùng: plate có thể về sau bản dịch ~100-200ms; che bằng màu nền
        // thật (thay vì navy đậm mặc định) để không thấy "cover đen" nhấp nháy trên trang sáng.
        const QImage plate = findRegionPlate(regionId, m_regionOcrHash.value(regionId, 0));
        const bool novelLayout = novelParagraphs != m_regionNovelParagraphs.constEnd();
        const QColor coverColor =
            novelLayout ? regionPaperColor(regionImage) : regionCoverColor(regionImage);
        for (EZTranslator::OverlayBox& overlay : boxes) {
            overlay.textColor = textColor;
            overlay.coverColor = coverColor;
            if (!novelLayout) {
                overlay.backdrop = regionImage;
                overlay.backdropRect = regionBounds;
            }
            // Novel mode keeps the old smart-cover design: the plate is the
            // whole page with glyphs erased, so the page texture/background
            // stays intact instead of a flat paper slab.
            if (!plate.isNull()) {
                overlay.plate = plate;
                overlay.plateRect = regionBounds;
            }
        }
    }

    if (!boxes.isEmpty()) {
        const qint64 translateMs =
            m_clock.isValid()
                ? m_clock.elapsed() - m_regionTranslateAtMs.value(regionId, 0)
                : -1;
        // Độ trễ NGƯỜI DÙNG THẤY: từ lúc ảnh nguồn đổi (bắt đầu ẩn overlay) tới lúc bản dịch hiện.
        const qint64 showMs =
            m_clock.isValid()
                ? m_clock.elapsed()
                      - m_regionLastChangeMs.value(regionId, m_clock.elapsed())
                : -1;
        qInfo().noquote() << "[Translate] overlay" << regionId
                          << "complete=" << document.complete
                          << "new=" << document.newTranslations
                          << "translatedUnits=" << document.paragraphs.size()
                          << "dịch" << translateMs << "ms | HIỆN SAU" << showMs
                          << "ms kể từ lúc nội dung đổi"
                          << "text=" << boxes.first().text.left(120);
        qInfo().noquote() << "[PIPE_METRIC] path=region stage=presentation request="
                          << token << "translationMs=" << translateMs
                          << "pipelineMs="
                          << (m_clock.isValid()
                                  ? m_clock.elapsed() - m_regionOcrAtMs.value(regionId, 0)
                                  : -1)
                          << "presentationMs=" << showMs
                          << "translationRuns=" << document.newTranslations
                          << "presented=1";
        m_regionBoxes.insert(regionId, boxes);
        m_regionScrolledOverlay.remove(regionId);
        const auto servedProfile = m_regionServedProfile.constFind(regionId);
        if (servedProfile != m_regionServedProfile.constEnd())
            m_regionDisplayProfile.insert(regionId, *servedProfile);

        // Bộ nhớ dịch theo đoạn: cuộn lên/xuống gặp lại cùng nội dung thì
        // giữ nguyên bản dịch cũ thay vì để engine dịch lại theo cách khác.
        QStringList translatedParagraphs = document.paragraphs;
        if (translatedParagraphs.size() < document.sourceParagraphs.size())
            translatedParagraphs.resize(document.sourceParagraphs.size());
        for (int i = 0; i < document.sourceParagraphs.size(); ++i) {
            const QString key = paragraphCacheKey(document.sourceParagraphs.at(i));
            if (key.isEmpty())
                continue;
            const QString fresh = translatedParagraphs.value(i).trimmed();
            const auto known = m_paragraphTranslations.constFind(key);
            if (known != m_paragraphTranslations.constEnd()
                && !known.value().trimmed().isEmpty()) {
                translatedParagraphs[i] = known.value();
            } else if (!fresh.isEmpty()) {
                if (m_paragraphTranslations.size() >= m_resourcePlan.paragraphCacheLines)
                    m_paragraphTranslations.clear();
                m_paragraphTranslations.insert(key, fresh);
            }
        }
        m_regionTranslatedText.insert(regionId,
                                      translatedParagraphs.join(QStringLiteral("\n\n")));
        if (m_floatingMode)
            refreshFloatingWindow();
        m_pendingSourceBoxes.remove(regionId);
        // Bản dịch của nội dung mới đã về: gỡ treo, hiện lại overlay ở ĐÚNG vị trí mới.
        m_overlaySuspended = false;
        updateOverlay(m_lastFrameSize);
    } else {
        qWarning() << "[Translate] không tạo được box overlay cho vùng" << regionId
                   << "sourceLines=" << lines->size()
                   << "paragraphs=" << document.sourceParagraphs.size()
                   << "translations=" << document.paragraphs.size();
    }
}

void AppController::suspendOverlayForChange(qint64 nowMs)
{
    // Ảnh nguồn vừa đổi: giữ bản dịch đang có trên màn hình trong lúc OCR/dịch nội dung mới
    // chạy nền. Khi kết quả mới về, onTranslationReady thay toàn bộ box bằng vị trí mới.
    // Xóa overlay tại đây làm chữ gốc lộ ra trong suốt thời gian OCR CPU chạy vài giây.
    m_overlaySuspended = true;
    m_overlaySuspendAtMs = nowMs;
}

void AppController::updateOverlay(const QSize& frameSize)
{
    if (!m_textOverlay)
        return;

    // Chế độ "Cửa sổ nổi": bản dịch chỉ hiện trong popup cạnh con trỏ — overlay phải ẩn
    // hẳn, nếu không người dùng thấy cùng lúc hai nơi hiển thị (điều đang bị báo lỗi).
    if (m_floatingMode) {
        if (m_textOverlay->isShowing())
            m_textOverlay->hideOverlay();
        return;
    }

    if (!m_overlayMode) {
        if (m_textOverlay->isShowing())
            m_textOverlay->hideOverlay();
        return;
    }

    // Đang chờ nội dung mới nhưng vẫn còn bản dịch cũ: tiếp tục vẽ bản dịch cũ.
    // Chỉ ẩn khi không còn box nào để vẽ.
    if (m_overlaySuspended && m_regionBoxes.isEmpty()) {
        if (m_textOverlay->isShowing())
            m_textOverlay->hideOverlay();
        return;
    }

    // Overlay chỉ hiện khi cửa sổ đích ở TIỀN CẢNH (xem tickOverlayVisibility). Pipeline vẫn chạy
    // nền khi người dùng ở app khác; ở đây chỉ chặn việc VẼ LẠI overlay khi cửa sổ đích bị che.
#ifdef _WIN32
    {
        const quintptr handle = m_mainWindow->selectedWindowHandle();
        if (handle == 0 || GetForegroundWindow() != reinterpret_cast<HWND>(handle)) {
            if (m_textOverlay->isShowing())
                m_textOverlay->hideOverlay();
            return;
        }
    }
#endif

    QList<EZTranslator::OverlayBox> all;
    for (auto it = m_regionBoxes.constBegin(); it != m_regionBoxes.constEnd(); ++it)
        all.append(it.value());

    const QRect client = targetClientRect();
    if (all.isEmpty() || frameSize.isEmpty() || !client.isValid() || client.isEmpty()) {
        if (m_textOverlay->isShowing())
            m_textOverlay->hideOverlay();
        return;
    }

    // Mixed regions: paper mode wins, because a glass panel drawn over a
    // reading column looks broken and hides the page texture.
    m_textOverlay->setGlassEnabled(m_regionNovelParagraphs.isEmpty());
    m_textOverlay->showOverTarget(client);
    m_textOverlay->setBoxes(all, frameSize);
}

void AppController::refreshFloatingWindow()
{
    if (!m_floatingWindow)
        return;

    // Cửa sổ nổi chỉ hiển thị vùng đang chọn (hoặc toàn cửa sổ nếu chưa vẽ vùng).
    // Không nối lịch sử: cuộn lên/xuống vẫn là một vùng hiện tại, nhưng bản dịch
    // của đoạn đã gặp được lấy lại từ m_paragraphTranslations.
    QString regionId = QStringLiteral("roi_full");
    if (!m_regions.isEmpty()) {
        int index = m_currentRegionIndex;
        if (index < 0 || index >= m_regions.size())
            index = 0;
        regionId = m_regions.at(index).id;
    }

    const QString source = m_regionSourceText.value(regionId).trimmed();
    const QString translated = m_regionTranslatedText.value(regionId).trimmed();
    m_floatingWindow->setSourceText(source);
    m_floatingWindow->setTranslatedText(translated);
    if (source.isEmpty())
        m_floatingWindow->setStatusText(QStringLiteral("Đang chờ văn bản OCR..."));
    else if (translated.isEmpty())
        m_floatingWindow->setStatusText(QStringLiteral("Đang dịch..."));
}

void AppController::verifyPendingRegions()
{
    if (m_regionVerifyPending.isEmpty()) {
        if (m_verifyTimer)
            m_verifyTimer->stop();
        return;
    }
    if (m_state != EZTranslator::TranslationState::Running || !m_ocrReady.load()
        || m_lastCapture.image.isNull())
        return;

    const qint64 now = m_clock.isValid() ? m_clock.elapsed() : 0;
    bool stable = false;
    for (const QString& id : m_regionVerifyPending) {
        if (now - m_regionLastChangeMs.value(id, now) >= 150) {
            stable = true;
            break;
        }
    }
    if (!stable)
        return;

    m_forceVerifyFrame = true;
    onCaptureFrame(m_lastCapture);
    m_forceVerifyFrame = false;

    // Vung da bi xoa/khong con OCR info: bo pending de timer khong chay mai.
    QSet<QString> stale;
    for (const QString& id : m_regionVerifyPending) {
        if (now - m_regionLastChangeMs.value(id, 0) > 5000)
            stale.insert(id);
    }
    for (const QString& id : stale)
        m_regionVerifyPending.remove(id);

    if (m_regionVerifyPending.isEmpty() && m_verifyTimer)
        m_verifyTimer->stop();
}

void AppController::tickOverlayVisibility()
{
    // Ẩn overlay khi cửa sổ đích KHÔNG còn ở tiền cảnh, hiện lại NGAY khi quay về - trong lúc đó
    // capture/OCR/dịch vẫn chạy nền (không dừng gì), nên quay lại không phải chờ detect.
    if (!m_textOverlay || !m_overlayMode)
        return;
    if (m_floatingMode) {
        if (m_textOverlay->isShowing())
            m_textOverlay->hideOverlay();
        return;
    }
    if (m_state != EZTranslator::TranslationState::Running)
        return;

    // A shifted translation is provisional until OCR confirms the new frame.
    // CPU recognition of a full reading region can take 8-9 seconds; expiring
    // it after 2.5 seconds reintroduced the very flicker scroll tracking avoids.
    if (m_clock.isValid()) {
        QStringList expired;
        for (const QString& regionId : m_regionScrolledOverlay) {
            if (m_clock.elapsed() - m_regionLastChangeMs.value(regionId, 0) > 10000)
                expired.append(regionId);
        }
        for (const QString& regionId : expired) {
            m_regionBoxes.remove(regionId);
            m_regionOverlayTrackers.remove(regionId);
            m_regionLastScrollMatchMs.remove(regionId);
            m_regionScrolledOverlay.remove(regionId);
        }
        if (!expired.isEmpty())
            updateOverlay(m_lastFrameSize);
    }

    bool foreground = true;
#ifdef _WIN32
    const quintptr handle = m_mainWindow->selectedWindowHandle();
    foreground = handle != 0 && GetForegroundWindow() == reinterpret_cast<HWND>(handle);
#endif
    if (!foreground) {
        if (m_textOverlay->isShowing())
            m_textOverlay->hideOverlay();
        return;
    }

    // Fallback 8s (nới từ 2s): bản dịch chậm là chuyện thường, 2s làm bản NGUỒN hiện lên trong khi
    // người dùng chỉ muốn thấy bản dịch. 8s chỉ còn là lưới an toàn khi engine lỗi hẳn.
    if (m_overlaySuspended && m_clock.isValid()
        && m_clock.elapsed() - m_overlaySuspendAtMs > 8000) {
        for (auto it = m_pendingSourceBoxes.constBegin(); it != m_pendingSourceBoxes.constEnd();
             ++it)
            m_regionBoxes.insert(it.key(), it.value());
        m_overlaySuspended = false;
    }

    // Trở lại tiền cảnh: hiện lại ngay với box đang có (không đợi OCR mới).
    if (!m_textOverlay->isShowing() && !m_regionBoxes.isEmpty() && !m_lastFrameSize.isEmpty())
        updateOverlay(m_lastFrameSize);
}

void AppController::onCaptureError(EZTranslator::CaptureError error, const QString& detail)
{
    qWarning().noquote() << "[CAPTURE_ERROR] code=" << static_cast<int>(error)
                         << "detail=" << detail;
    using CE = EZTranslator::CaptureError;
    switch (error) {
    case CE::WindowClosed:
        // Helper WGC có thể thoát khi cửa sổ bị che/thu nhỏ tạm thời. Nếu cửa sổ VẪN tồn tại thì
        // khởi động lại capture và GIỮ pipeline chạy nền; chỉ dừng hẳn khi cửa sổ thật sự đóng.
#ifdef _WIN32
        if (m_mainWindow && m_mainWindow->selectedWindowHandle() != 0
            && IsWindow(reinterpret_cast<HWND>(m_mainWindow->selectedWindowHandle()))) {
            startCapture(m_mainWindow->selectedWindowHandle());
            break;
        }
#endif
        m_mainWindow->setStatus("Cửa sổ đã đóng", QColor("#f59e0b"));
        if (m_state == EZTranslator::TranslationState::Running) {
            onStopTranslation();
        }
        break;
    case CE::WindowMinimized:
        // Không làm gì - capture tiếp tục khi window được restore
        break;
    case CE::PermissionDenied:
        m_mainWindow->setStatus("Không có quyền capture cửa sổ này", QColor("#ef4444"));
        if (m_state == EZTranslator::TranslationState::Running) {
            onStopTranslation();
        }
        break;
    default:
        qDebug() << "[Capture] error:" << static_cast<int>(error) << detail;
        break;
    }
}
