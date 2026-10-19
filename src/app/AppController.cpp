#include "AppController.h"
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
#include "ui/RoiPreviewDialog.h"

#include <QFileInfo>

using namespace EZTranslator::OverlayLayout;

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <QApplication>
#include <QGuiApplication>
#include <QPainter>
#include <QScreen>
#include <QMessageBox>
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

} // namespace

AppController::AppController(QObject* parent)
    : QObject(parent)
{
    // Nạp TOÀN BỘ cài đặt đã lưu (mọi nhóm, không chỉ nhóm Dịch thuật) qua SettingsStore.
    EZTranslator::SettingsStore::load(m_settings);
    m_clock.start();
    qDebug() << "Creating MainWindow...";
    m_mainWindow = new MainWindow();
    m_mainWindow->setLanguages(m_settings.sourceLanguage, m_settings.targetLanguage);
    // Chế độ hiển thị ban đầu do MainWindow quyết định (card "Cửa sổ nổi" được chọn sẵn).
    m_overlayMode = m_mainWindow->displayMode() == MainWindow::DisplayMode::ScreenOverlay;
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
        m_regionLines.clear();
        m_regionStateAtMs.clear();
        m_regionServedHash.clear();
        m_regionServedProfile.clear();
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
                m_mainWindow->setStatus(
                    mode == MainWindow::DisplayMode::ScreenOverlay
                        ? QStringLiteral("Hiển thị: Overlay trên màn hình")
                        : QStringLiteral("Hiển thị: Cửa sổ nổi (rê chuột lên chữ)"),
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
            if (m_wgcCapture)
                m_wgcCapture->stop();
            if (m_gdiCapture && m_gdiCapture->start(m_captureHandle)) {
                m_capture = m_gdiCapture;
                qDebug() << "[AppController] Capture backend: GDI (fallback)";
            }
        });
        return true;
    }

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
QString ocrModelsDir()
{
    const QByteArray override = qgetenv("EZ_MODELS_DIR");
    if (!override.isEmpty())
        return QString::fromLocal8Bit(override);
    return QStringLiteral(EZ_DEFAULT_MODELS_DIR);
}

} // namespace

EZTranslator::OcrOptions AppController::ocrOptions() const
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
    options.useGpu = m_settings.ocrUseGpu;
    options.binarize = m_settings.ocrBinarize;

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

    const EZTranslator::OcrOptions options = ocrOptions();
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
    spawnOcrWorker(m_hoverOcrThread, m_hoverOcrWorker, ocrOptions(), m_ocrGeneration,
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
    } else {
        connect(worker, &EZTranslator::OcrWorker::regionRecognized,
                this, &AppController::onRegionRecognized, Qt::QueuedConnection);
        connect(worker, &EZTranslator::OcrWorker::regionPlateReady,
                this, &AppController::onRegionPlateReady, Qt::QueuedConnection);
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
    m_regionLinePitch.clear();
    m_regionParagraphRatio.clear();
    m_regionBoxes.clear();
    m_regionSourceText.clear();
    m_regionLines.clear();
    m_regionStateAtMs.clear();
    m_regionServedHash.clear();
    m_regionServedProfile.clear();
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
    if (m_ocrProvider == QLatin1String("CPU"))
        m_mainWindow->setStatus(QStringLiteral("OCR: CPU (không có GPU DX12/DirectML)"),
                                QColor("#f59e0b"));
    else
        m_mainWindow->setStatus(QStringLiteral("OCR: ") + m_ocrProvider, QColor("#22c55e"));

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
    config.cacheCapacity = m_settings.cacheCapacityLines;
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
    ++m_hoverTranslationToken; // vô hiệu hoá kết quả dịch đang về
}

void AppController::syncOverlayMode()
{
    // NGUỒN SỰ THẬT là UI (MainWindow::displayMode). Đọc lại mỗi lần để hai chế độ KHÔNG BAO GIỜ
    // chạy cùng lúc: trước đây chuyển sang Overlay mà cờ cũ còn false (signal lỡ/lệch lúc khởi
    // động) ⇒ hover vẫn chạy ⇒ popup hiện đè lên overlay.
    const bool overlay = m_mainWindow->displayMode() == MainWindow::DisplayMode::ScreenOverlay;
    if (overlay == m_overlayMode)
        return;
    m_overlayMode = overlay;

    if (m_overlayMode) {
        // Sang overlay: tắt hẳn popup hover — hai nơi không hiện cùng lúc.
        stopHover();
        if (m_hoverPopup)
            m_hoverPopup->hidePopup();
        updateOverlay(m_lastFrameSize);
    } else {
        // Sang cửa sổ nổi: tắt overlay đè lên game, bật lại hover.
        if (m_textOverlay)
            m_textOverlay->hideOverlay();
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
    m_overlayMode = m_mainWindow->displayMode() == MainWindow::DisplayMode::ScreenOverlay;

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
                        && m_ocrReady.load() && !m_hoverFrame.isNull() && !m_overlayMode;
    if (!active) {
        note(QStringLiteral("chưa đủ điều kiện (chưa bật dịch / OCR chưa xong / chưa có frame)"));
        m_hoverPopup->hidePopup();
        m_hoverLineValid = false;
        m_hoverPending = false;
        return;
    }

#ifdef _WIN32
    const quintptr handle = m_mainWindow->selectedWindowHandle();
    if (handle == 0 || GetForegroundWindow() != reinterpret_cast<HWND>(handle)) {
        note(QStringLiteral("cửa sổ đích KHÔNG ở tiền cảnh"));
        m_hoverPopup->hidePopup();
        m_hoverLineValid = false;
        m_hoverPending = false;
        return;
    }

    POINT cursorNative = {};
    if (!GetCursorPos(&cursorNative))
        return;
    const QPoint screenPoint(cursorNative.x, cursorNative.y);

    const QRect client = m_hoverClientPhysical;
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
    sendHoverProbe(screenPoint, /*prefetch=*/false);
#endif
}

void AppController::sendHoverProbe(const QPoint& screenPoint, bool prefetch, int stage)
{
    // Worker hover chỉ sinh khi vào chế độ "Cửa sổ nổi"; trong 1-2s đầu nó còn nạp model thì
    // bỏ probe (không gửi vào engine chưa nạp xong).
    if (!m_hoverOcrWorker || !m_hoverOcrReady)
        return;
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

    // QUAY VỀ BẢN CŨ: luôn dùng dải CỐ ĐỊNH 1200×96 cho mọi probe (bỏ thang mở rộng —
    // đó là thứ gây ra loạt lỗi vừa rồi). Tham số `stage` giữ lại nhưng không dùng.
    Q_UNUSED(stage);
    const int useStage = 0;
    const int wantedW = kBandStages[useStage].width;
    const int wantedH = kBandStages[useStage].height;
    const int bandW = qMin(qMax(240, frameW), wantedW);
    const int bandH = qMin(qMax(48, frameH), wantedH);
    const int bandX = qBound(0, fx - bandW / 2, qMax(0, frameW - bandW));
    const QRect srcRect(bandX, fy - bandH / 2, bandW, bandH);

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

    m_hoverLastSendMs = m_clock.isValid() ? m_clock.elapsed() : 0;
    m_hoverSentCursor = screenPoint;

    const QPoint cursorLocal(fx - srcRect.x(), fy - srcRect.y());
    QMetaObject::invokeMethod(m_hoverOcrWorker, "probeHover", Qt::QueuedConnection,
                              Q_ARG(QString, QString::number(token)),
                              Q_ARG(QImage, bandImage), Q_ARG(QPoint, cursorLocal));
#else
    Q_UNUSED(screenPoint);
    Q_UNUSED(prefetch);
    Q_UNUSED(stage);
#endif
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
        m_hoverPopup->showTranslation(text, cached, logical, sourceWidth);
        return;
    }

    // Chưa có bản dịch: KHÔNG hiện dòng gốc trước (tránh "hai giai đoạn"); đợi worker
    // dịch xong rồi hiện đồng thời ở onTranslationReady.
    m_hoverTranslation.clear();
    m_hoverPending = true;
    m_hoverProbeMs = m_clock.isValid() ? m_clock.elapsed() : 0;

    if (!ensureTranslator()) {
        m_hoverPending = false;
        m_hoverPopup->showTranslation(text, QString(), logical, sourceWidth);
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
                              Q_ARG(quint64, token));
}

void AppController::onHoverProbed(const QString& id, const QString& text, const QRect& lineRect,
                                  bool clippedByBand)
{
    if (!m_hoverPopup)
        return;

    const quint64 token = id.toULongLong();
    const auto geomIt = m_hoverProbes.constFind(token);
    if (geomIt == m_hoverProbes.constEnd())
        return; // probe cũ / không rõ hình học
    const HoverProbeGeom geom = geomIt.value();
    m_hoverProbes.erase(geomIt);

    const bool hasLine = !lineRect.isNull() && !text.isEmpty();

    // Cụm chữ con trỏ đang ở vẫn CHẠM mép dải ⇒ câu còn dài hơn dải: MỞ RỘNG dải rồi dò
    // lại (tối đa vài bước). Dừng khi cụm kết thúc ở KHE — biên tự nhiên do khoảng đệm
    // tới phần tử khác quyết định, đúng cách meikipop làm (họ đọc cả vùng nên không cắt).
    // Chỉ làm cho probe "cuối"; probe dò trước (con trỏ đang đi) không tốn thêm gì.
    if (hasLine && clippedByBand && !geom.prefetch && geom.canGrow
        && geom.stage + 1 < kBandStageCount) {
        qDebug() << "[Hover] cụm chạm mép dải → mở rộng bước" << (geom.stage + 1);
        sendHoverProbe(geom.cursorScreen, /*prefetch=*/false, geom.stage + 1);
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
    if (geom.prefetch)
        return;
    if (token != m_hoverLastFinalToken)
        return; // đã có yêu cầu cuối mới hơn

    if (!hasLine) {
        m_hoverPopup->hidePopup();
        m_hoverLineValid = false;
        m_hoverPending = false;
        return;
    }
    if (text == m_hoverSource && m_hoverLineValid
        && (m_hoverPending || !m_hoverTranslation.isEmpty())) {
        return;
    }

    m_hoverProbedCursor = geom.cursorScreen;
    showHoverLine(text, m_hoverPrefetch.lineClient, geom.cursorScreen);
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
    m_overlayMode = m_mainWindow->displayMode() == MainWindow::DisplayMode::ScreenOverlay;

    m_state = EZTranslator::TranslationState::Running;
    m_mainWindow->onTranslationStarted();
    m_trayManager->updateState(true);

    // Capture chạy trước (nhẹ) để pipeline hoạt động sớm; nạp model OCR (nặng: DirectML
    // init + warm-up) ở BACKGROUND nên UI không đứng vài giây như trước.
    if (m_capture) {
        startCapture(m_mainWindow->selectedWindowHandle());
    }
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
                                  Q_ARG(QString, QStringLiteral("Hello.")), Q_ARG(quint64, 0));
    }
}

void AppController::onStopTranslation()
{
    m_state = EZTranslator::TranslationState::Idle;
    m_mainWindow->onTranslationStopped();
    m_trayManager->updateState(false);

    if (m_textOverlay)
        m_textOverlay->hideOverlay();

    // "Tự động xóa cửa sổ" (Cài đặt → Giao diện): dừng dịch là xoá luôn nội dung + cache
    // theo nội dung của mọi vùng — lần bật sau quét lại từ đầu thay vì hiện lại bản cũ.
    if (m_settings.autoClearWindow) {
        for (const EZTranslator::TranslationRegion& region : m_regions)
            forgetRegion(region.id);
        m_ocrBoxCache.clear();
        m_regionOcrHash.clear();
        m_regionRequestSource.clear();
        m_pendingSourceBoxes.clear();
        m_overlaySuspended = false;
    }

    if (m_visibilityTimer)
        m_visibilityTimer->stop();
    if (m_catchUpTimer)
        m_catchUpTimer->stop();

    stopHover();

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
    m_regionLines.remove(regionId);
    m_regionStateAtMs.remove(regionId);
    m_regionServedHash.remove(regionId);
    m_regionServedProfile.remove(regionId);
    m_regionLineTexts.remove(regionId);
    m_regionToken.remove(regionId);
    m_regionOcrInfo.remove(regionId);
    m_regionOcrToken.remove(regionId);
    m_regionLinePitch.remove(regionId);
    m_regionParagraphRatio.remove(regionId);
    m_regionImages.remove(regionId);
    m_regionPlates.remove(regionId);
    m_regionOcrAtMs.remove(regionId);
    m_regionTranslateAtMs.remove(regionId);
    m_regionScreenRects.remove(regionId);
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
                                 || updated.ocrBinarize != m_settings.ocrBinarize;

    m_settings = updated;
    EZTranslator::SettingsStore::save(m_settings);
    m_hoverCache.clear(); // engine/prompt có thể đổi → không dùng bản dịch hover cũ

    applySettings(/*initial=*/false);

    // Cài đặt có thể đổi chế độ hiển thị → đồng bộ lại (tắt hover nếu đang Overlay…).
    syncOverlayMode();

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
    if (m_hoverPopup)
        EZTranslator::Platform::setExcludedFromCapture(quintptr(m_hoverPopup->winId()),
                                                       m_settings.excludeFromCapture);
    if (m_miniFloatBar)
        EZTranslator::Platform::setExcludedFromCapture(quintptr(m_miniFloatBar->winId()),
                                                       m_settings.excludeFromCapture);

    // ── Hiệu suất capture ────────────────────────────────────────────────────
    if (m_wgcCapture)
        m_wgcCapture->setFps(m_settings.captureFps);
    if (m_gdiCapture)
        m_gdiCapture->setFps(m_settings.captureFps);

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
    m_regionLinePitch.clear();
    m_regionParagraphRatio.clear();
    m_regionBoxes.clear();
    m_regionSourceText.clear();
    m_regionLines.clear();
    m_regionStateAtMs.clear();
    m_regionServedHash.clear();
    m_regionServedProfile.clear();
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
    if (m_state == EZTranslator::TranslationState::Running || m_captureStartedForPreview) {
        if (m_capture) m_capture->stop();
        if (handle != 0) {
            startCapture(handle);
        }
    }
}

void AppController::onCaptureFrame(const EZTranslator::CapturedFrame& frame)
{
    m_gotFirstFrame = true;
    m_lastCapture = frame; // giữ lại cho timer đuổi kịp (cửa sổ tĩnh không phát frame mới)

    // Lưu frame mới nhất cho chế độ "dịch khi chỉ chuột" (dò dòng dưới con trỏ).
    m_hoverFrame = frame.image;
    m_hoverFrameSize = frame.image.size();
    m_hoverClientPhysical = targetClientRectPhysical();

    // Model OCR đang được nạp ở background → chưa xử lý gì (overlay để trống).
    if (!m_ocrReady.load()) {
        if (m_textOverlay && m_textOverlay->isShowing())
            m_textOverlay->hideOverlay();
        return;
    }

    if (m_regionManager.isEmpty()) {
        if (m_textOverlay && m_textOverlay->isShowing())
            m_textOverlay->hideOverlay();
        return;
    }

    const QList<EZTranslator::RegionFrame> regionFrames = m_regionManager.extractRegions(frame);

    // Cập nhật lên cửa sổ Preview ROI nếu người dùng đang mở
    if (m_roiPreviewDialog && m_roiPreviewDialog->isVisible()) {
        m_roiPreviewDialog->updateFrames(regionFrames);
    }

    m_ocrScheduler.beginTick();
    const qint64 now = m_clock.isValid() ? m_clock.elapsed() : 0;

    QSize frameSize;
    for (const EZTranslator::RegionFrame& regionFrame : regionFrames) {
        if (!regionFrame.isValid())
            continue;

        frameSize = regionFrame.sourceFrameSize;

        // Hash nội dung vùng — dùng cho cả đường tăng dần lẫn cache box bên dưới.
        const quint64 regionHash = EZTranslator::imageDifferenceHash(regionFrame.image);

        // Lên lịch: chỉ OCR khi vùng đã settle, qua cooldown, và tối đa vài lần mỗi tick.
        const EZTranslator::OcrScheduleDecision decision =
            m_ocrScheduler.process(regionFrame.regionId, regionFrame.image, now);

        if (decision.changed) {
            // Nội dung vùng vừa ĐỔI (đang cuộn / đổi màn hình): ẩn overlay NGAY. Chỉ hiện lại khi
            // có BẢN DỊCH của nội dung mới - không hiện bản detect nguồn (cách translator xử lý:
            // tắt khi ảnh nguồn đổi, hiện lại đúng vị trí mới khi neo xong).
            m_regionLastChangeMs.insert(regionFrame.regionId, now);
            suspendOverlayForChange(now);
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
                if (!moved)
                    continue; // đứng yên thật → không làm gì (trang tĩnh: 0% CPU/GPU)
                forceOcr = true;
            }
        }

        // (Đã BỎ đường OCR TĂNG DẦN khỏi tick: box xấp xỉ của nó KHÔNG bao giờ được hiện — overlay
        // ẩn khi nguồn đổi rồi hiện khi có box chính xác — nên nó chỉ còn hai tác dụng phụ đều có
        // hại: (a) OCR dải bằng BlockingQueuedConnection chặn luồng UI ~20-50ms mỗi khung lúc cuộn;
        // (b) gửi yêu cầu dịch mỗi khung làm hàng đợi worker dịch nghẽn ~200ms (log thật: "dịch"
        // của lượt cache-hit cũng 200ms). Bộ máy dịch giờ: cache dịch theo CÂU + 2 phát emit.)

        // CỬA SỔ CHUYỂN ĐỘNG 80ms: ảnh còn đổi trong 80ms thì không chạy OCR full (chặn churn giữa
        // lúc cuộn) — nhưng ngắn thôi, vì mỗi ms ở đây là độ trễ người dùng thấy sau khi dừng.
        if (now - m_regionLastChangeMs.value(regionFrame.regionId, qint64(-1000000)) < 80)
            continue;

        // CACHE BOX THEO NỘI DUNG: nội dung này đã từng OCR thì dùng lại box ngay - KHÔNG OCR lại.
        // So hash theo NGƯỠNG nhiễu (không đòi khớp tuyệt đối): nhiễu capture 1-4 bit từng làm
        // cache trượt mỗi khung dù mắt thường thấy y hệt. Cuộn qua lại vùng đã xem là tức thì
        // (bài học OcrRegionCache của bản translator).
        // forceOcr (nội dung dịch nhẹ nhưng THẬT): KHÔNG dùng cache box — cache so hash thô có thể
        // trả box của nội dung CŨ (lệch ≤4 bit) ⇒ vị trí vẫn sai; phải OCR lại chính ảnh hiện tại.
        const auto cachedRegion = forceOcr ? m_ocrBoxCache.constEnd()
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
                m_regionImages.insert(regionFrame.regionId, regionFrame.image);
                m_regionOcrHash.insert(regionFrame.regionId, regionHash);
                m_regionOcrAtMs.insert(regionFrame.regionId, now);
                onRegionRecognized(regionFrame.regionId, *hitBoxes);
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
        m_regionImages.insert(regionFrame.regionId, regionFrame.image);
        m_regionOcrHash.insert(regionFrame.regionId, regionHash);
        m_regionOcrAtMs.insert(regionFrame.regionId, now);
        // Đánh dấu bản đang phục vụ: các khung sau chỉ nhiễu ≤ maxDistance sẽ bị bỏ qua ở đầu tick.
        m_regionServedHash.insert(regionFrame.regionId, regionHash);
        // Báo worker biết có yêu cầu mới đang xếp hàng (worker bỏ dựng plate khi còn yêu cầu chờ).
        m_ocrWorker->noteRequestQueued();
        QMetaObject::invokeMethod(m_ocrWorker, "recognizeRegion", Qt::QueuedConnection,
                                  Q_ARG(QString, regionFrame.regionId),
                                  Q_ARG(QImage, regionFrame.image));
    }

    m_lastFrameSize = frameSize;
    updateOverlay(frameSize);
}

void AppController::onRegionRecognized(const QString& regionId,
                                       const QList<EZTranslator::OcrTextBox>& boxes)
{
    handleRegionBoxes(regionId, boxes, /*buildPlate=*/true);
}

void AppController::onRegionPlateReady(const QString& regionId, quint64 imageHash,
                                       const QImage& plate)
{
    if (plate.isNull())
        return;

    QHash<quint64, QImage>& pool = m_regionPlates[regionId];
    if (pool.size() >= 3)
        pool.clear(); // bounded: giữ vài nội dung mới nhất (cuộn qua lại vẫn trúng)
    pool.insert(imageHash, plate);

    // Overlay đang hiện nội dung này mà box chưa có plate (đang che tạm bằng màu đặc) thì dán
    // plate vào và vẽ lại — nâng cấp nền "màu đặc" → "nền đã xoá chữ", không chờ thêm gì.
    const auto boxesIt = m_regionBoxes.find(regionId);
    if (boxesIt == m_regionBoxes.end() || boxesIt->isEmpty())
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
                                      bool buildPlate, bool approximate)
{
    // BẢN SAO ngay đầu hàm: đường cache-hit truyền vào CHÍNH list đang nằm trong m_ocrBoxCache,
    // mà cuối hàm này lại insert/clear cache đó — rehash/clear giải phóng node và mọi tham chiếu
    // tới `boxesIn` thành dangling (use-after-free → crash 0xc0000005 trong Qt6Core sau một lúc
    // dùng overlay, khi cache đủ 8 nội dung và bị clear).
    const QList<EZTranslator::OcrTextBox> boxes = boxesIn;

    const auto info = m_regionOcrInfo.constFind(regionId);
    if (info == m_regionOcrInfo.constEnd())
        return;
    if (boxes.isEmpty())
        return; // không đọc được gì → giữ kết quả trước, không xóa overlay

    const QRect pixelRect = info->pixelRect;
    const qint64 ocrMs = m_clock.isValid() ? m_clock.elapsed() - m_regionOcrAtMs.value(regionId, 0)
                                           : -1;
    qDebug().noquote() << "[OCR]" << regionId
                       << (approximate ? QStringLiteral("[tăng dần]") : QString()) << "->"
                       << boxes.size() << "line(s), sau" << ocrMs << "ms";

    // Ghim nhịp dòng + khe đoạn cho vùng này (hysteresis kiểu bản gốc): đo trên frame
    // hiện tại nhưng chỉ cập nhật khi lệch đáng kể. Nhờ vậy cấu trúc đoạn không nhấp
    // nháy khi box rung, và cache dịch không bị trượt mỗi frame.
    // Box XẤP XỈ (đường tăng dần) không được ghim: ước lượng cuộn làm nhịp đo sai, ghim vào là
    // đầu độc cả những lượt CHÍNH XÁC sau (assembly chia dòng khác đi ⇒ vị trí đoạn lệch).
    if (!approximate) {
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
                    if (regionCache.size() >= 8)
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
            if (regionCache.size() >= 8)
                regionCache.clear(); // bounded: giữ các nội dung mới nhất
            regionCache.insert(hash, boxes);
        }
    }

    // Ghi PROFILE 1D của ảnh đã phục vụ: lần sau hash trong ngưỡng nhiễu thì so profile để biết
    // nội dung có DỊCH thật không (hash 8×8 quá thô — xem khối "NỘI DUNG Y HỆT" ở tick).
    if (buildPlate) {
        const QImage regionImage = m_regionImages.value(regionId);
        if (!regionImage.isNull()) {
            m_regionServedProfile.insert(
                regionId, grayProfile(regionImage, regionImage.rect(), 4, /*withGradients=*/true));
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
                              Q_ARG(quint64, requestToken));
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
        const QString translated = document.paragraphs.value(0).trimmed();
        if (m_clock.isValid())
            qDebug() << "[Hover] translated in" << (m_clock.elapsed() - m_hoverProbeMs) << "ms"
                     << "source=" << m_hoverSource.left(40);

        if (translated.isEmpty()) {
            // Không dịch được → vẫn hiện dòng gốc để không im lặng.
            m_hoverPopup->showTranslation(m_hoverSource, QString(),
                                          cursorToLogical(m_hoverLineCursor),
                                          sizeToLogical(m_hoverLineClient.width()));
            return;
        }

        m_hoverTranslation = translated;
        m_hoverCache.insert(m_hoverSource, translated);
        // Hiện ĐỒNG THỜI nguồn + bản dịch, chỉ một lần (không còn hai giai đoạn).
        m_hoverPopup->showTranslation(m_hoverSource, translated,
                                      cursorToLogical(m_hoverLineCursor),
                                      sizeToLogical(m_hoverLineClient.width()));
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

    const auto lines = m_regionLines.constFind(regionId);
    if (lines == m_regionLines.constEnd())
        return;

    const QRect regionBounds = m_regionOcrInfo.contains(regionId)
                                   ? m_regionOcrInfo.value(regionId).pixelRect
                                   : QRect();
    QList<EZTranslator::OverlayBox> boxes = buildParagraphBoxes(
        document, *lines, m_regionLineTexts.value(regionId), regionBounds,
        /*skipUntranslated=*/true);
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
        const QColor coverColor = regionCoverColor(regionImage);
        const QImage plate = findRegionPlate(regionId, m_regionOcrHash.value(regionId, 0));
        for (EZTranslator::OverlayBox& overlay : boxes) {
            overlay.textColor = textColor;
            overlay.coverColor = coverColor;
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
        m_regionBoxes.insert(regionId, boxes);
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
    // Ảnh nguồn vừa đổi: ẩn overlay ngay (không hiện bản detect nguồn), hẹn giờ fallback để nếu
    // bản dịch không về trong 2s thì hiện tạm bản nguồn đã detect (xem tickOverlayVisibility).
    m_overlaySuspended = true;
    m_overlaySuspendAtMs = nowMs;
    if (m_textOverlay && m_textOverlay->isShowing())
        m_textOverlay->hideOverlay();
}

void AppController::updateOverlay(const QSize& frameSize)
{
    if (!m_textOverlay)
        return;

    // Chế độ "Cửa sổ nổi": bản dịch chỉ hiện trong popup cạnh con trỏ — overlay phải ẩn
    // hẳn, nếu không người dùng thấy cùng lúc hai nơi hiển thị (điều đang bị báo lỗi).
    if (!m_overlayMode) {
        if (m_textOverlay->isShowing())
            m_textOverlay->hideOverlay();
        return;
    }

    // Đang treo vì ảnh nguồn vừa đổi: không vẽ gì (chờ bản dịch của nội dung mới).
    if (m_overlaySuspended) {
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

    m_textOverlay->showOverTarget(client);
    m_textOverlay->setBoxes(all, frameSize);
}

void AppController::tickOverlayVisibility()
{
    // Ẩn overlay khi cửa sổ đích KHÔNG còn ở tiền cảnh, hiện lại NGAY khi quay về - trong lúc đó
    // capture/OCR/dịch vẫn chạy nền (không dừng gì), nên quay lại không phải chờ detect.
    if (!m_textOverlay || !m_overlayMode)
        return;
    if (m_state != EZTranslator::TranslationState::Running)
        return;

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
    Q_UNUSED(detail)
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
