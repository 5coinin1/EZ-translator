#include "AppController.h"
#include "capture/GdiWindowCapture.h"
#include "capture/WgcWindowCapture.h"
#include "ocr/OcrTextAssembler.h"
#include "overlay/TextOverlay.h"
#include "region/RegionManager.h"
#include "ui/MainWindow.h"
#include "ui/MiniFloatBar.h"
#include "ui/RegionEditorWindow.h"
#include "ui/RoiPreviewDialog.h"
#include "ui/SettingsDialog.h"
#include "ui/TrayIconManager.h"
#include "ui/region/RegionHighlightOverlay.h"
#include "ui/region/RegionSnipperOverlay.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <QApplication>
#include <QDebug>
#include <QGuiApplication>
#include <QMessageBox>
#include <QScreen>
#include <QTimer>

AppController::AppController(QObject *parent) : QObject(parent)
{
    qDebug() << "Creating MainWindow...";
    m_mainWindow = new MainWindow();
    qDebug() << "Creating RegionSnipperOverlay...";
    m_snipperOverlay = new RegionSnipperOverlay();
    qDebug() << "Creating RegionHighlightOverlay...";
    m_highlightOverlay = new RegionHighlightOverlay();
    qDebug() << "Creating RegionEditorWindow...";
    m_regionEditor = new RegionEditorWindow();
    qDebug() << "Creating SettingsDialog...";
    m_settingsDialog = new SettingsDialog(m_mainWindow);
    qDebug() << "Creating MiniFloatBar...";
    m_miniFloatBar = new MiniFloatBar();
    qDebug() << "Creating TrayIconManager...";
    m_trayManager = new TrayIconManager(this);
    qDebug() << "Creating WgcWindowCapture...";
    m_wgcCapture = new EZTranslator::WgcWindowCapture(this);
    qDebug() << "Creating GdiWindowCapture...";
    m_gdiCapture = new EZTranslator::GdiWindowCapture(this);
    m_capture =
        m_wgcCapture; // mặc định ưu tiên WGC, fallback trong startCapture()
    qDebug() << "Creating TextOverlay...";
    m_textOverlay = new EZTranslator::TextOverlay();
    qDebug() << "Setting mock regions...";

    initConnections();
}

AppController::~AppController()
{
    delete m_mainWindow;
    delete m_regionEditor;
    delete m_snipperOverlay;
    delete m_highlightOverlay;
    delete m_miniFloatBar;
    delete m_roiPreviewDialog;
    delete m_textOverlay;
}

void AppController::initConnections()
{
    // MainWindow signals
    connect(m_mainWindow, &MainWindow::requestStartTranslation, this,
            &AppController::onStartTranslation);
    connect(m_mainWindow, &MainWindow::requestStopTranslation, this,
            &AppController::onStopTranslation);
    connect(m_mainWindow, &MainWindow::requestOpenRegionEditor, this,
            &AppController::onOpenRegionEditor);
    connect(m_mainWindow, &MainWindow::requestClearRegion, this, [this]()
            {
    m_regions.clear();
    m_regionManager.clear();
    m_changeDetector.reset();
    m_regionTextBoxes.clear();
    if (m_textOverlay)
      m_textOverlay->hideOverlay();
    if (m_highlightOverlay) {
      m_highlightOverlay->hideOverlay();
    }
    m_mainWindow->setShowRegionActive(false); });
    connect(m_mainWindow, &MainWindow::requestShowRegion, this,
            &AppController::onShowRegion);
    connect(m_mainWindow, &MainWindow::requestPreviewRoi, this,
            &AppController::onPreviewRoiRequested);
    connect(m_mainWindow, &MainWindow::requestOpenSettings, this,
            &AppController::onOpenSettings);
    // Khi user chon window moi -> bat dau/chuyen capture
    connect(m_mainWindow, &MainWindow::targetWindowSelected, this,
            &AppController::onTargetWindowSelected);

    // Capture signals -> AppController (QueuedConnection dam bao cross-thread an
    // toan) Cả 2 backend cùng nối; chỉ backend đang chạy mới phát signal.
    EZTranslator::IWindowCapture *const backends[] = {m_wgcCapture, m_gdiCapture};
    for (EZTranslator::IWindowCapture *capture : backends)
    {
        connect(capture, &EZTranslator::IWindowCapture::frameReady, this,
                &AppController::onCaptureFrame, Qt::QueuedConnection);
        connect(capture, &EZTranslator::IWindowCapture::captureError, this,
                &AppController::onCaptureError, Qt::QueuedConnection);
    }

    // HighlightOverlay signals
    connect(m_highlightOverlay, &RegionHighlightOverlay::closed, this,
            [this]()
            { m_mainWindow->setShowRegionActive(false); });

    // RegionSnipperOverlay signals
    connect(m_snipperOverlay, &RegionSnipperOverlay::regionSnapped, this,
            &AppController::onRegionSnapped);
    connect(m_snipperOverlay, &RegionSnipperOverlay::cancelled, this,
            &AppController::onRegionSnapCancelled);

    // RegionEditorWindow signals
    connect(m_regionEditor, &RegionEditorWindow::saved, this,
            &AppController::onRegionEditorSaved);
    connect(m_regionEditor, &RegionEditorWindow::cancelled, this,
            &AppController::onRegionEditorCancelled);

    // MiniFloatBar signals
    connect(m_miniFloatBar, &MiniFloatBar::requestStopTranslation, this,
            &AppController::onStopTranslation);
    connect(m_miniFloatBar, &MiniFloatBar::requestOpenSettings, this,
            &AppController::onOpenSettings);

    // TrayIconManager signals
    connect(m_trayManager, &TrayIconManager::requestShowMainWindow, this,
            &AppController::onShowMainWindow);
    connect(m_trayManager, &TrayIconManager::requestToggleTranslation, this,
            &AppController::onToggleTranslation);
    connect(m_trayManager, &TrayIconManager::requestOpenSettings, this,
            &AppController::onOpenSettings);
    connect(m_trayManager, &TrayIconManager::requestQuit, this,
            &AppController::onQuit);

    // SettingsDialog signals
    connect(m_settingsDialog, &SettingsDialog::settingsSaved, this,
            &AppController::onSettingsSaved);
}

bool AppController::startCapture(quintptr windowHandle)
{
    if (windowHandle == 0)
        return false;

    // Ưu tiên Windows.Graphics.Capture; nếu không start được (OS cũ, helper lỗi,
    // cửa sổ không hỗ trợ) thì fallback sang GDI PrintWindow.
    if (m_wgcCapture && m_wgcCapture->start(windowHandle))
    {
        m_capture = m_wgcCapture;
        qDebug() << "[AppController] Capture backend: WGC";
        return true;
    }

    if (m_gdiCapture && m_gdiCapture->start(windowHandle))
    {
        m_capture = m_gdiCapture;
        qDebug() << "[AppController] Capture backend: GDI (fallback)";
        return true;
    }

    qWarning() << "[AppController] No capture backend could start";
    return false;
}

namespace
{

    /** Thư mục model OCR: ưu tiên biến môi trường EZ_MODELS_DIR, mặc định trong
     * repo. */
    QString ocrModelsDir()
    {
        const QByteArray override = qgetenv("EZ_MODELS_DIR");
        if (!override.isEmpty())
            return QString::fromLocal8Bit(override);
        return QStringLiteral(EZ_DEFAULT_MODELS_DIR);
    }

} // namespace

bool AppController::ensureOcrLoaded()
{
    if (m_ocrEngine.isLoaded())
        return true;
    if (m_ocrLoadAttempted)
        return false;
    m_ocrLoadAttempted = true;

    const QString dir = ocrModelsDir();
    const bool english = m_settings.sourceLanguage.startsWith(
        QStringLiteral("en"), Qt::CaseInsensitive);

    EZTranslator::OcrOptions options;
    options.detModelPath = dir + QStringLiteral("/ch_PP-OCRv4_det_infer.onnx");
    if (english)
    {
        options.recModelPath = dir + QStringLiteral("/en_PP-OCRv4_rec_mobile.onnx");
        options.dictionaryPath = dir + QStringLiteral("/en_dict.txt");
    }
    else
    {
        options.recModelPath = dir + QStringLiteral("/ch_PP-OCRv4_rec_infer.onnx");
        options.dictionaryPath = dir + QStringLiteral("/ppocr_keys_v1.txt");
    }
    options.useGpu = m_settings.ocrUseGpu;

    QString error;
    if (!m_ocrEngine.load(options, &error))
    {
        qWarning() << "[OCR] Load failed:" << error;
        m_mainWindow->setStatus(QStringLiteral("Không nạp được OCR: ") + error,
                                QColor("#ef4444"));
        return false;
    }

    const QString provider = m_ocrEngine.providerName();
    qInfo() << "[OCR] Loaded. Provider =" << provider
            << "det =" << options.detModelPath << "rec =" << options.recModelPath;

    // Cho người dùng biết đang chạy GPU hay CPU (máy không có GPU rời sẽ là CPU).
    if (provider == QLatin1String("CPU"))
        m_mainWindow->setStatus(
            QStringLiteral("OCR: CPU (không có GPU DX12/DirectML)"),
            QColor("#f59e0b"));
    else
        m_mainWindow->setStatus(QStringLiteral("OCR: ") + provider,
                                QColor("#22c55e"));
    return true;
}

QRect AppController::targetClientRect() const
{
    QRect rect;
#ifdef _WIN32
    const quintptr handle = m_mainWindow->selectedWindowHandle();
    if (handle != 0)
    {
        HWND targetHwnd = reinterpret_cast<HWND>(handle);
        if (IsWindow(targetHwnd))
        {
            RECT clientRc = {};
            POINT origin = {0, 0};
            if (GetClientRect(targetHwnd, &clientRc) &&
                ClientToScreen(targetHwnd, &origin))
            {
                const int physW = clientRc.right - clientRc.left;
                const int physH = clientRc.bottom - clientRc.top;
                qreal dpr = 1.0;
                if (QScreen *screen = QGuiApplication::primaryScreen())
                    dpr = screen->devicePixelRatio();
                if (dpr <= 0.0)
                    dpr = 1.0;
                if (physW > 0 && physH > 0)
                {
                    rect = QRect(qRound(origin.x / dpr), qRound(origin.y / dpr),
                                 qRound(physW / dpr), qRound(physH / dpr));
                }
            }
        }
    }
#endif
    return rect;
}

void AppController::start()
{
    m_mainWindow->show();
    m_mainWindow->raise();
    m_mainWindow->activateWindow();
}

void AppController::onStartTranslation()
{
    if (m_state == EZTranslator::TranslationState::Running)
        return;

    const quintptr handle = m_mainWindow->selectedWindowHandle();
    if (handle == 0)
    {
        m_mainWindow->showSelectWindowWarning();
        return;
    }

    // Chỉ bắt đầu khi đã nạp được mô hình OCR.
    // ensureOcrLoaded() tự hiển thị thông báo nếu nạp thất bại.
    if (!ensureOcrLoaded())
        return;

    // Preview có thể đã khởi động capture trước đó.
    if (!m_capture || !m_capture->isRunning())
    {
        if (!startCapture(handle))
        {
            m_mainWindow->setStatus(
                QStringLiteral("Không khởi động được chụp cửa sổ."),
                QColor("#ef4444"));
            return;
        }
    }

    // Một lần chạy mới phải nhận dạng lại cả ảnh không thay đổi.
    m_changeDetector.reset();
    m_regionTextBoxes.clear();

    if (m_textOverlay)
        m_textOverlay->hideOverlay();

    m_captureStartedForPreview = false;
    m_state = EZTranslator::TranslationState::Running;

    m_mainWindow->onTranslationStarted();
    m_trayManager->updateState(true);

    qInfo() << "[Pipeline] Started";
}

void AppController::onStopTranslation()
{
    // Đổi trạng thái trước để các frame đang chờ không tiếp tục OCR.
    m_state = EZTranslator::TranslationState::Idle;

    m_mainWindow->onTranslationStopped();
    m_trayManager->updateState(false);

    m_changeDetector.reset();
    m_regionTextBoxes.clear();

    if (m_textOverlay)
        m_textOverlay->hideOverlay();

    const bool previewVisible =
        m_roiPreviewDialog && m_roiPreviewDialog->isVisible();

    if (m_capture && m_capture->isRunning())
    {
        if (previewVisible)
        {
            // Chỉ giữ capture phục vụ xem ảnh.
            m_captureStartedForPreview = true;
        }
        else
        {
            m_capture->stop();
            m_captureStartedForPreview = false;
        }
    }
    else
    {
        m_captureStartedForPreview = false;
    }

    qInfo() << "[Pipeline] Stopped";
}

void AppController::onToggleTranslation()
{
    if (m_state == EZTranslator::TranslationState::Running)
    {
        onStopTranslation();
    }
    else
    {
        onStartTranslation();
    }
}

void AppController::onOpenRegionEditor()
{
    quintptr handle = m_mainWindow->selectedWindowHandle();
    if (handle == 0)
    {
        m_mainWindow->showSelectWindowWarning();
        return;
    }

    QRect targetRect;

#ifdef _WIN32
    if (handle != 0)
    {
        HWND targetHwnd = reinterpret_cast<HWND>(handle);
        if (IsWindow(targetHwnd))
        {
            HWND rootHwnd = GetAncestor(targetHwnd, GA_ROOT);
            if (rootHwnd && IsWindow(rootHwnd))
            {
                targetHwnd = rootHwnd;
            }

            if (IsIconic(targetHwnd))
            {
                ShowWindow(targetHwnd, SW_RESTORE);
            }
            SetForegroundWindow(targetHwnd);
            BringWindowToTop(targetHwnd);

            // Lấy client area (nội dung thực tế cần dịch) sang toạ độ màn hình
            // (physical px)
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
            if (QScreen *screen = QGuiApplication::primaryScreen())
            {
                dpr = screen->devicePixelRatio();
            }
            if (dpr <= 0.0)
                dpr = 1.0;

            targetRect = QRect(qRound(physX / dpr), qRound(physY / dpr),
                               qRound(physW / dpr), qRound(physH / dpr));
            qDebug() << "[AppController] TargetWindow:" << (void *)targetHwnd
                     << "physClient:" << physX << physY << physW << physH
                     << "dpr:" << dpr << "targetRect:" << targetRect;
        }
    }
#endif

    // 1. Tạm ẩn MainWindow để chụp màn hình
    m_mainWindow->hide();

    // 2. Mở overlay sau một khoảng trễ ngắn
    QTimer::singleShot(200, this, [this, targetRect]()
                       {
    QPixmap screenshot;
    if (QScreen *screen = QGuiApplication::primaryScreen()) {
      screenshot = screen->grabWindow(0);
    }
    m_snipperOverlay->startSnipping(screenshot, targetRect); });
}

void AppController::onRegionSnapped(const EZTranslator::NormalizedRect &rect,
                                    const QRect &screenRect)
{
    EZTranslator::TranslationRegion reg;
    reg.id = QString("roi_%1").arg(m_regions.size() + 1);
    reg.orderNumber = m_regions.size() + 1;
    reg.name = QString("Vùng dịch %1").arg(reg.orderNumber);
    reg.normalizedRect = rect;
    reg.tagColor = QColor(37, 99, 235);

    m_regions.clear();
    m_regions.append(reg);
    m_regionManager.setRegions(m_regions);
    m_changeDetector.reset();
    m_regionTextBoxes.clear();
    m_currentScreenRect = screenRect;

    if (m_highlightOverlay && m_highlightOverlay->isVisible())
    {
        m_highlightOverlay->hideOverlay();
    }

    m_mainWindow->show();
    m_mainWindow->raise();
    m_mainWindow->activateWindow();
#ifdef _WIN32
    SetForegroundWindow(reinterpret_cast<HWND>(m_mainWindow->winId()));
#endif
    m_mainWindow->setRegion(rect, screenRect);
    m_mainWindow->setShowRegionActive(false);
}

void AppController::onRegionSnapCancelled()
{
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
    if (m_highlightOverlay->isVisible())
    {
        m_highlightOverlay->hideOverlay();
        m_mainWindow->setShowRegionActive(false);
        return;
    }

    if (!m_mainWindow->hasRegion())
    {
        m_mainWindow->setStatus(
            "Chưa chọn vùng dịch! Bấm 'Chọn vùng (F6)' để quét vùng.",
            QColor("#f59e0b"));
        return;
    }

    // 1. Ưu tiên tọa độ màn hình thực tế (pixel tuyệt đối) mà người dùng vừa quét
    // Đảm bảo 100% khớp tuyệt đối với vị trí vừa chọn trên màn hình
    QRect screenRect = m_mainWindow->currentScreenRect();
    if (!screenRect.isValid() && m_currentScreenRect.isValid())
    {
        screenRect = m_currentScreenRect;
    }

    if (screenRect.isValid() && screenRect.width() >= 10 &&
        screenRect.height() >= 10)
    {
        m_highlightOverlay->showScreenRect(screenRect, 8000);
        m_mainWindow->setShowRegionActive(true);
        return;
    }

    // 2. Fallback: nếu chỉ có tọa độ chuẩn hóa (NormalizedRect)
    EZTranslator::NormalizedRect roi = m_mainWindow->currentRegion();
    QRect targetRect;
#ifdef _WIN32
    quintptr handle = m_mainWindow->selectedWindowHandle();
    if (handle != 0)
    {
        HWND targetHwnd = reinterpret_cast<HWND>(handle);
        if (IsWindow(targetHwnd))
        {
            RECT clientRc = {};
            GetClientRect(targetHwnd, &clientRc);
            POINT pt = {0, 0};
            ClientToScreen(targetHwnd, &pt);

            int physX = pt.x;
            int physY = pt.y;
            int physW = clientRc.right - clientRc.left;
            int physH = clientRc.bottom - clientRc.top;

            qreal dpr = 1.0;
            if (QScreen *screen = QGuiApplication::primaryScreen())
            {
                dpr = screen->devicePixelRatio();
            }
            if (dpr <= 0.0)
                dpr = 1.0;

            targetRect = QRect(qRound(physX / dpr), qRound(physY / dpr),
                               qRound(physW / dpr), qRound(physH / dpr));
        }
    }
#endif

    if (targetRect.isEmpty() || targetRect.width() < 100 ||
        targetRect.height() < 100)
    {
        if (QScreen *s = QGuiApplication::primaryScreen())
        {
            targetRect = s->geometry();
        }
    }

    m_highlightOverlay->showRegion(targetRect, roi, 8000);
    m_mainWindow->setShowRegionActive(true);
}

void AppController::onPreviewRoiRequested()
{
    const quintptr handle = m_mainWindow->selectedWindowHandle();
    if (handle == 0)
    {
        m_mainWindow->showSelectWindowWarning();
        return;
    }

    if (!m_roiPreviewDialog)
    {
        m_roiPreviewDialog = new EZTranslator::RoiPreviewDialog(nullptr);

        connect(m_roiPreviewDialog, &EZTranslator::RoiPreviewDialog::closed, this,
                &AppController::onPreviewClosed);
    }

    if (!m_capture || !m_capture->isRunning())
    {
        if (!startCapture(handle))
        {
            m_mainWindow->setStatus(
                QStringLiteral("Không khởi động được chụp ảnh để xem trước."),
                QColor("#ef4444"));
            return;
        }
    }

    // Nếu chưa chạy OCR, capture hiện chỉ phục vụ Preview.
    m_captureStartedForPreview =
        m_state != EZTranslator::TranslationState::Running;

    m_roiPreviewDialog->show();
    m_roiPreviewDialog->raise();
    m_roiPreviewDialog->activateWindow();
}

void AppController::onPreviewClosed()
{
    if (m_captureStartedForPreview)
    {
        if (m_capture && m_state != EZTranslator::TranslationState::Running)
        {
            m_capture->stop();
        }
        m_captureStartedForPreview = false;
    }
}

void AppController::onRegionEditorSaved(
    const QList<EZTranslator::TranslationRegion> &regions)
{
    m_regions = regions;
    m_regionManager.setRegions(regions);
    m_changeDetector.reset();
    m_regionTextBoxes.clear();
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
    m_settingsDialog->show();
    m_settingsDialog->raise();
    m_settingsDialog->activateWindow();
}

void AppController::onSettingsSaved(const EZTranslator::AppSettings &settings)
{
    m_settings = settings;
}

void AppController::onShowMainWindow()
{
    m_mainWindow->show();
    m_mainWindow->raise();
    m_mainWindow->activateWindow();
}

void AppController::onQuit() { qApp->quit(); }

void AppController::onTargetWindowSelected(quintptr handle,
                                           const QString & /*title*/,
                                           const QString & /*processName*/)
{
    if (m_state == EZTranslator::TranslationState::Running ||
        m_captureStartedForPreview)
    {
        if (m_capture)
            m_capture->stop();
        if (handle != 0)
        {
            startCapture(handle);
        }
    }
}

void AppController::onCaptureFrame(const EZTranslator::CapturedFrame &frame)
{
    if (frame.image.isNull())
        return;

    const bool previewVisible =
        m_roiPreviewDialog && m_roiPreviewDialog->isVisible();

    const bool running = m_state == EZTranslator::TranslationState::Running;

    // Không chạy OCR và cũng không xem trước: bỏ qua frame đang chờ.
    if (!running && !previewVisible)
    {
        if (m_textOverlay)
            m_textOverlay->hideOverlay();
        return;
    }

    QList<EZTranslator::RegionFrame> regionFrames;

    if (m_regionManager.isEmpty())
    {
        // Vùng tạm dùng toàn bộ ảnh, không thêm vào vùng chọn của người dùng.
        EZTranslator::RegionFrame fullFrame;
        fullFrame.regionId = QStringLiteral("__full_window__");
        fullFrame.regionName = QStringLiteral("Toàn cửa sổ");
        fullFrame.regionOrder = 1;
        fullFrame.pixelRect = frame.image.rect();
        fullFrame.sourceFrameSize = frame.image.size();
        fullFrame.image = frame.image;
        fullFrame.timestamp = frame.timestamp;

        regionFrames.append(fullFrame);
    }
    else
    {
        regionFrames = m_regionManager.extractRegions(frame);
    }

    // Preview chỉ cần ảnh, không cần OCR.
    if (previewVisible)
        m_roiPreviewDialog->updateFrames(regionFrames);

    // Capture phục vụ Preview vẫn có thể chạy sau khi bấm Dừng.
    // Tuy nhiên, không được tiếp tục OCR hoặc hiển thị overlay.
    if (!running || !m_ocrEngine.isLoaded())
    {
        if (m_textOverlay)
            m_textOverlay->hideOverlay();
        return;
    }

    if (regionFrames.isEmpty())
    {
        m_regionTextBoxes.clear();

        if (m_textOverlay)
            m_textOverlay->hideOverlay();

        return;
    }

    for (const EZTranslator::RegionFrame &regionFrame : regionFrames)
    {
        if (!regionFrame.isValid())
            continue;

        // Chỉ OCR lại khi hình ảnh của vùng có thay đổi.
        if (!m_changeDetector.hasChanged(regionFrame.regionId, regionFrame.image))
        {
            continue;
        }

        const QList<EZTranslator::OcrTextBox> boxes =
            m_ocrEngine.recognize(regionFrame.image);

        const QString text = EZTranslator::OcrTextAssembler::assemble(boxes);

        const EZTranslator::OcrTimings timings = m_ocrEngine.lastTimings();

        qDebug() << "[OCR]" << regionFrame.regionId << "->" << boxes.size()
                 << "line(s),"
                 << "det" << timings.detMs << "ms,"
                 << "rec" << timings.recMs << "ms"
                 << "(cached" << timings.recCached << ")";

        if (!text.isEmpty())
            qDebug().noquote() << "[OCR text]" << text;

        // Đổi tọa độ chữ từ ảnh vùng chọn sang ảnh toàn cửa sổ.
        QList<EZTranslator::OcrTextBox> frameBoxes;
        frameBoxes.reserve(boxes.size());

        for (const EZTranslator::OcrTextBox &box : boxes)
        {
            EZTranslator::OcrTextBox mapped = box;
            mapped.rect = box.rect.translated(regionFrame.pixelRect.topLeft());

            frameBoxes.append(mapped);
        }

        m_regionTextBoxes.insert(regionFrame.regionId, frameBoxes);

        // Bước tiếp theo: dịch văn bản rồi cập nhật nội dung hiển thị.
    }

    updateOverlay(frame.image.size());
}

void AppController::updateOverlay(const QSize &frameSize)
{
    if (!m_textOverlay)
        return;

    QList<EZTranslator::OcrTextBox> all;
    for (auto it = m_regionTextBoxes.constBegin();
         it != m_regionTextBoxes.constEnd(); ++it)
        all.append(it.value());

    const QRect client = targetClientRect();
    if (all.isEmpty() || frameSize.isEmpty() || !client.isValid() ||
        client.isEmpty())
    {
        if (m_textOverlay->isShowing())
            m_textOverlay->hideOverlay();
        return;
    }

    m_textOverlay->showOverTarget(client);
    m_textOverlay->setTextBoxes(all, frameSize);
}

void AppController::onCaptureError(EZTranslator::CaptureError error,
                                   const QString &detail)
{
    Q_UNUSED(detail)
    using CE = EZTranslator::CaptureError;
    switch (error)
    {
    case CE::WindowClosed:
        m_mainWindow->setStatus("Cửa sổ đã đóng", QColor("#f59e0b"));
        if (m_state == EZTranslator::TranslationState::Running)
        {
            onStopTranslation();
        }
        break;
    case CE::WindowMinimized:
        // Không làm gì - capture tiếp tục khi window được restore
        break;
    case CE::PermissionDenied:
        m_mainWindow->setStatus("Không có quyền capture cửa sổ này",
                                QColor("#ef4444"));
        if (m_state == EZTranslator::TranslationState::Running)
        {
            onStopTranslation();
        }
        break;
    default:
        qDebug() << "[Capture] error:" << static_cast<int>(error) << detail;
        break;
    }
}
