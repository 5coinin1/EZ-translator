#include "AppController.h"
#include "translation/OfflineTranslator.h"

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
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QLabel>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QStringList>
#include <QVBoxLayout>

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

    initTranslationUi();
    initConnections();
}

AppController::~AppController()
{
    if (m_capture)
        m_capture->stop();
    delete m_translator;
    m_translator = nullptr;

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
    resetTranslationSession();
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
    QList<EZTranslator::OcrTextBox>
    buildTranslationBlocks(const QList<EZTranslator::OcrTextBox> &boxes)
    {
        const auto document = EZTranslator::OcrTextAssembler::assembleDocument(boxes);

        const QStringList rows = document.text.split('\n', Qt::KeepEmptyParts);

        int nonEmptyRows = 0;

        for (const auto &row : rows)
        {
            if (!row.trimmed().isEmpty())
                ++nonEmptyRows;
        }

        QList<EZTranslator::OcrTextBox> result;

        // Nếu văn bản và tọa độ không khớp, giữ từng dòng riêng
        // để tránh đặt bản dịch vào sai vị trí.
        if (nonEmptyRows != document.lines.size())
        {
            for (const auto &line : document.lines)
            {
                EZTranslator::OcrTextBox box;
                box.text = line.text.simplified();
                box.rect = line.rect;

                if (!box.text.isEmpty() && !box.rect.isEmpty())
                    result.append(box);
            }

            return result;
        }

        // Nhận biết đầu mục: 1. / 2) / - / * / •
        static const QRegularExpression itemStart(
            QStringLiteral(R"(^(?:[0-9]{1,3}[.)]|[-*•])\s+)"));

        static const QRegularExpression sentenceEnd(
            QStringLiteral(R"([.!?…]["'”’)]*$)"));

        EZTranslator::OcrTextBox block;
        EZTranslator::OcrTextBox previous;

        int count = 0;
        int index = 0;

        const auto flush = [&]()
        {
            if (!block.text.isEmpty())
                result.append(block);

            block = EZTranslator::OcrTextBox{};
            previous = EZTranslator::OcrTextBox{};
            count = 0;
        };

        for (const auto &row : rows)
        {
            // Dòng trống do bộ ghép OCR tạo ra là dấu ngắt đoạn.
            if (row.trimmed().isEmpty())
            {
                flush();
                continue;
            }

            const auto &line = document.lines.at(index++);

            EZTranslator::OcrTextBox current;
            current.text = line.text.simplified();
            current.rect = line.rect;

            if (current.text.isEmpty() || current.rect.isEmpty())
            {
                flush();
                continue;
            }

            if (count > 0)
            {
                const bool differentSize =
                    current.rect.height() > previous.rect.height() * 1.4 ||
                    previous.rect.height() > current.rect.height() * 1.4;

                const bool separateColumn = current.rect.left() > block.rect.right() ||
                                            current.rect.right() < block.rect.left();

                const bool previousEndsSentence =
                    sentenceEnd.match(previous.text).hasMatch();

                // Dòng ngắn đứng riêng trước dòng dài có thể là tiêu đề.
                const bool shortHeading =
                    count == 1 && previous.text.size() <= 100 && !previousEndsSentence &&
                    previous.rect.width() < current.rect.width() * 0.65;

                // Dòng cuối ngắn và có dấu kết câu:
                // ưu tiên tách đoạn tiếp theo.
                const bool paragraphEnded =
                    previousEndsSentence &&
                    previous.rect.width() < block.rect.width() * 0.72;

                const bool newItem = itemStart.match(current.text).hasMatch();

                // Giới hạn kích thước mỗi khối; phần còn lại
                // sẽ được chuyển sang khối tiếp theo.
                const bool tooLong = block.text.size() + 1 + current.text.size() > 900;

                if (differentSize || separateColumn || shortHeading || paragraphEnded ||
                    newItem || tooLong)
                {
                    flush();
                }
            }

            if (count == 0)
            {
                block = current;
            }
            else
            {
                block.text += QLatin1Char(' ');
                block.text += current.text;
                block.rect = block.rect.united(current.rect);
            }

            previous = current;
            ++count;
        }

        flush();
        return result;
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

    m_settings.sourceLanguage = "en";
    m_settings.translationEngine = "offline";

    if (!ensureOcrLoaded())
        return;

    resetTranslationSession();

    QDir root(QStringLiteral(EZ_DEFAULT_MODELS_DIR));
    root.cdUp();

    QString error;

    if (!m_translator->start(root.absolutePath(), &error))
    {
        m_mainWindow->setStatus(error, QColor("#ef4444"));
        return;
    }

    if ((!m_capture || !m_capture->isRunning()) && !startCapture(handle))
    {
        m_mainWindow->setStatus("Không khởi động được chụp cửa sổ.",
                                QColor("#ef4444"));
        return;
    }

    m_captureStartedForPreview = false;
    m_state = EZTranslator::TranslationState::Running;

    m_mainWindow->onTranslationStarted();
    m_trayManager->updateState(true);

    m_translatedText->setPlaceholderText("Đang chờ văn bản OCR...");

    if (m_mainWindow->displayMode() == MainWindow::DisplayMode::FloatingWindow)
    {
        m_translationWindow->show();
        m_translationWindow->raise();
    }
    else
    {
        m_translationWindow->hide();
    }

    m_mainWindow->setStatus(m_translator->isReady()
                                ? "Offline Anh → Việt đã sẵn sàng."
                                : "Đang nạp bộ dịch offline...",
                            QColor("#22c55e"));

    qInfo() << "[Pipeline] Started";
}

void AppController::onStopTranslation()
{
    m_state = EZTranslator::TranslationState::Idle;

    resetTranslationSession();

    m_mainWindow->onTranslationStopped();
    m_trayManager->updateState(false);

    m_translatedText->setPlaceholderText("Đã dừng.");

    const bool preview = m_roiPreviewDialog && m_roiPreviewDialog->isVisible();

    m_captureStartedForPreview = false;

    if (m_capture && m_capture->isRunning())
    {
        if (preview)
            m_captureStartedForPreview = true;
        else
            m_capture->stop();
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
    resetTranslationSession();
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

    resetTranslationSession();

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

void AppController::onTargetWindowSelected(quintptr handle, const QString &,
                                           const QString &)
{
    onStopTranslation();

    if (m_capture)
        m_capture->stop();

    m_captureStartedForPreview = false;

    m_mainWindow->clearRegion();

    if (handle != 0 && m_roiPreviewDialog && m_roiPreviewDialog->isVisible())
    {
        if (startCapture(handle))
            m_captureStartedForPreview = true;
    }

    resetTranslationSession();

    m_mainWindow->setStatus("Đã đổi cửa sổ. Chọn vùng nếu cần rồi bấm Bắt đầu.");
}

void AppController::onCaptureFrame(const EZTranslator::CapturedFrame &frame)
{
    if (sender() && sender() != m_capture)
        return;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    if (frame.image.isNull() || frame.timestamp < m_acceptFramesAfter ||
        now - frame.timestamp > 1000)
    {
        return;
    }

    const bool preview = m_roiPreviewDialog && m_roiPreviewDialog->isVisible();

    const bool running = m_state == EZTranslator::TranslationState::Running;

    if (!running && !preview)
        return;

    QList<EZTranslator::RegionFrame> regions;

    if (m_regionManager.isEmpty())
    {
        EZTranslator::RegionFrame full;

        full.regionId = "__full_window__";
        full.regionName = "Toàn cửa sổ";
        full.regionOrder = 1;
        full.pixelRect = frame.image.rect();
        full.sourceFrameSize = frame.image.size();
        full.image = frame.image;
        full.timestamp = frame.timestamp;

        regions.append(full);
    }
    else
    {
        regions = m_regionManager.extractRegions(frame);
    }

    if (preview)
        m_roiPreviewDialog->updateFrames(regions);

    if (!running || !m_ocrEngine.isLoaded() || !m_translator->isReady())
    {
        return;
    }

    // Phát hiện thay đổi trước khi giới hạn nhịp OCR.
    for (const auto &region : regions)
    {
        if (region.isValid() &&
            (!m_ocrImages.contains(region.regionId) ||
             m_ocrImages.value(region.regionId) != region.image))
        {
            m_overlayFresh = false;
            m_textOverlay->hide();
            break;
        }
    }

    if (now < m_nextOcrAt)
        return;

    m_nextOcrAt = now + 400;

    QList<EZTranslator::OcrTextBox> layout;
    QStringList sources;

    for (const auto &region : regions)
    {
        if (!region.isValid())
            continue;

        if (!m_ocrImages.contains(region.regionId) ||
            m_ocrImages.value(region.regionId) != region.image)
        {
            const auto boxes = m_ocrEngine.recognize(region.image);
            const auto blocks = buildTranslationBlocks(boxes);

            QList<EZTranslator::OcrTextBox> mapped;

            for (const auto &block : blocks)
            {
                EZTranslator::OcrTextBox box = block;

                box.rect = block.rect.intersected(region.image.rect())
                               .translated(region.pixelRect.topLeft());

                if (!box.text.isEmpty() && !box.rect.isEmpty())
                    mapped.append(box);
            }

            m_regionTextBoxes.insert(region.regionId, mapped);
            m_ocrImages.insert(region.regionId, region.image);

            qDebug() << "[OCR blocks]"
                     << "region =" << region.regionId << "blocks =" << mapped.size();
        }

        for (const auto &box : m_regionTextBoxes.value(region.regionId))
        {
            layout.append(box);
            sources.append(box.text);
        }
    }

    m_overlayLayout = layout;
    m_overlayFrameSize = frame.image.size();
    m_overlayFresh = true;

    const QString text = sources.join('\n');

    if (text == m_lastSubmittedText)
    {
        if (!m_lastTranslatedText.isEmpty())
        {
            // Chữ không đổi nhưng vị trí có thể đã thay đổi.
            applyTranslationResult(m_lastTranslatedText);
        }
        else
        {
            m_overlayOutput = m_overlayLayout;

            for (auto &box : m_overlayOutput)
                box.text.clear();

            updateOverlay(m_overlayFrameSize);
        }

        return;
    }

    m_lastSubmittedText = text;
    m_lastTranslatedText.clear();

    m_sourceText->setPlainText(sources.join(QStringLiteral("\n\n")));
    m_translatedText->clear();

    qDebug().noquote() << "[OCR text]" << text;

    // Che các dòng chữ trong lúc chờ dịch.
    m_overlayOutput = m_overlayLayout;

    for (auto &box : m_overlayOutput)
        box.text.clear();

    updateOverlay(m_overlayFrameSize);

    if (text.isEmpty())
    {
        m_translator->cancel();

        m_translatedText->setPlaceholderText("Chưa nhận dạng được chữ.");

        return;
    }

    m_translatedText->setPlaceholderText("Đang dịch...");
    m_translator->submit(text);
}

void AppController::updateOverlay(const QSize &frameSize)
{
    if (!m_textOverlay)
        return;

    if (m_state != EZTranslator::TranslationState::Running ||
        m_mainWindow->displayMode() != MainWindow::DisplayMode::ScreenOverlay ||
        !m_overlayFresh || m_overlayOutput.isEmpty() || frameSize.isEmpty())
    {
        m_textOverlay->hide();
        return;
    }

#ifdef _WIN32
    const HWND target =
        reinterpret_cast<HWND>(m_mainWindow->selectedWindowHandle());

    if (!IsWindow(target) || IsIconic(target))
    {
        m_textOverlay->hide();
        return;
    }

    RECT client{};

    if (!GetClientRect(target, &client) ||
        QSize(client.right - client.left, client.bottom - client.top) !=
            frameSize)
    {
        m_textOverlay->hide();
        return;
    }

    const HWND foreground = GetForegroundWindow();

    DWORD foregroundPid = 0;
    GetWindowThreadProcessId(foreground, &foregroundPid);

    if (GetAncestor(foreground, GA_ROOT) != GetAncestor(target, GA_ROOT) &&
        foregroundPid != GetCurrentProcessId())
    {
        m_textOverlay->hide();
        return;
    }
#endif

    const QRect clientRect = targetClientRect();

    if (!clientRect.isValid() || clientRect.isEmpty())
    {
        m_textOverlay->hide();
        return;
    }

    m_textOverlay->setTextBoxes(m_overlayOutput, frameSize);

    m_textOverlay->showOverTarget(clientRect);
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

void AppController::initTranslationUi()
{
    m_translator = new OfflineTranslator(this);

    m_translationWindow =
        new QDialog(m_mainWindow, Qt::Window | Qt::WindowStaysOnTopHint);

    m_translationWindow->setWindowTitle("EZ Translator - Offline Anh -> Viet");
    m_translationWindow->resize(680, 520);

    m_translationWindow->setStyleSheet(
        "QDialog { background:#111827; color:#e5e7eb; }"
        "QLabel { color:#e5e7eb; }"
        "QPlainTextEdit { background:#1f2937; color:#f9fafb;"
        "border:1px solid #374151; padding:8px; font-size:14px; }");

    auto *layout = new QVBoxLayout(m_translationWindow);

    layout->addWidget(new QLabel("Văn bản OCR — tiếng Anh"));

    m_sourceText = new QPlainTextEdit;
    m_sourceText->setReadOnly(true);
    layout->addWidget(m_sourceText, 1);

    layout->addWidget(new QLabel("Bản dịch — tiếng Việt"));

    m_translatedText = new QPlainTextEdit;
    m_translatedText->setReadOnly(true);
    layout->addWidget(m_translatedText, 1);

    connect(m_translationWindow, &QDialog::rejected, this,
            &AppController::onStopTranslation);

    connect(m_translator, &OfflineTranslator::ready, this, [this]()
            {
    if (m_state == EZTranslator::TranslationState::Running) {
      m_mainWindow->setStatus("Offline Anh → Việt đã sẵn sàng.",
                              QColor("#22c55e"));
    } });

    connect(m_translator, &OfflineTranslator::translated, this,
            [this](const QString &source, const QString &target)
            {
                if (m_state != EZTranslator::TranslationState::Running ||
                    source != m_lastSubmittedText)
                {
                    return;
                }

                applyTranslationResult(target);
                qDebug().noquote() << "[Translation]" << target;
            });

    connect(m_translator, &OfflineTranslator::failed, this,
            [this](const QString &message)
            {
                onStopTranslation();

                m_mainWindow->setStatus("Lỗi dịch: " + message, QColor("#ef4444"));

                m_translatedText->setPlainText("Lỗi dịch: " + message);
            });

    connect(m_mainWindow, &MainWindow::displayModeChanged, this,
            [this](MainWindow::DisplayMode mode)
            {
                if (mode == MainWindow::DisplayMode::ScreenOverlay)
                {
                    m_translationWindow->hide();
                    updateOverlay(m_overlayFrameSize);
                }
                else
                {
                    m_textOverlay->hideOverlay();

                    if (m_state == EZTranslator::TranslationState::Running)
                    {
                        m_translationWindow->show();
                        m_translationWindow->raise();
                    }
                }
            });
    auto *overlayTimer = new QTimer(this);
    overlayTimer->setInterval(100);

    connect(overlayTimer, &QTimer::timeout, this,
            [this]()
            { updateOverlay(m_overlayFrameSize); });

    overlayTimer->start();
}

void AppController::resetTranslationSession()
{
    m_acceptFramesAfter = QDateTime::currentMSecsSinceEpoch() + 1;

    m_nextOcrAt = 0;

    m_changeDetector.reset();
    m_regionTextBoxes.clear();

    m_ocrImages.clear();
    m_ocrTexts.clear();
    m_lastSubmittedText.clear();

    m_overlayLayout.clear();
    m_overlayOutput.clear();
    m_overlayFrameSize = {};
    m_lastTranslatedText.clear();
    m_overlayFresh = false;

    if (m_translator)
        m_translator->cancel();

    if (m_sourceText)
        m_sourceText->clear();

    if (m_translatedText)
        m_translatedText->clear();

    if (m_textOverlay)
        m_textOverlay->hideOverlay();
}

void AppController::applyTranslationResult(const QString &target)
{
    // Giữ dữ liệu gốc để tái sử dụng khi chỉ thay đổi vị trí.
    m_lastTranslatedText = target;

    const QStringList blocks = target.split('\n', Qt::KeepEmptyParts);

    // Cửa sổ nổi: cách các đoạn bằng một dòng trống.
    const QString displayText = blocks.join(QStringLiteral("\n\n"));

    if (m_translatedText->toPlainText() != displayText)
        m_translatedText->setPlainText(displayText);

    // Mỗi khối dịch phải có đúng một khung tương ứng.
    if (blocks.size() != m_overlayLayout.size())
    {
        m_overlayOutput.clear();
        updateOverlay(m_overlayFrameSize);

        m_mainWindow->setStatus("Số khối dịch không khớp. "
                                "Chọn Cửa sổ nổi để xem kết quả.",
                                QColor("#f59e0b"));

        return;
    }

    m_overlayOutput = m_overlayLayout;

    for (int i = 0; i < blocks.size(); ++i)
        m_overlayOutput[i].text = blocks[i];

    updateOverlay(m_overlayFrameSize);
}