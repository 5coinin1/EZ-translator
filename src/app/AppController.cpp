#include "AppController.h"
#include "ui/MainWindow.h"
#include "ui/RegionEditorWindow.h"
#include "ui/region/RegionSnipperOverlay.h"
#include "ui/region/RegionHighlightOverlay.h"
#include "ui/SettingsDialog.h"
#include "ui/MiniFloatBar.h"
#include "ui/TrayIconManager.h"
#include "capture/GdiWindowCapture.h"
#include "region/RegionManager.h"
#include "ui/RoiPreviewDialog.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <QApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QMessageBox>
#include <QTimer>
#include <QDebug>

AppController::AppController(QObject* parent)
    : QObject(parent)
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
    qDebug() << "Creating GdiWindowCapture...";
    m_capture = new EZTranslator::GdiWindowCapture(this);
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
}

void AppController::initConnections()
{
    // MainWindow signals
    connect(m_mainWindow, &MainWindow::requestStartTranslation, this, &AppController::onStartTranslation);
    connect(m_mainWindow, &MainWindow::requestStopTranslation,  this, &AppController::onStopTranslation);
    connect(m_mainWindow, &MainWindow::requestOpenRegionEditor, this, &AppController::onOpenRegionEditor);
    connect(m_mainWindow, &MainWindow::requestClearRegion,      this, [this]() {
        m_regions.clear();
        m_regionManager.clear();
        if (m_highlightOverlay) {
            m_highlightOverlay->hideOverlay();
        }
        m_mainWindow->setShowRegionActive(false);
    });
    connect(m_mainWindow, &MainWindow::requestShowRegion,       this, &AppController::onShowRegion);
    connect(m_mainWindow, &MainWindow::requestPreviewRoi,       this, &AppController::onPreviewRoiRequested);
    connect(m_mainWindow, &MainWindow::requestOpenSettings,     this, &AppController::onOpenSettings);
    // Khi user chon window moi -> bat dau/chuyen capture
    connect(m_mainWindow, &MainWindow::targetWindowSelected,    this, &AppController::onTargetWindowSelected);

    // Capture signals -> MainWindow preview (QueuedConnection dam bao cross-thread an toan)
    connect(m_capture, &EZTranslator::GdiWindowCapture::frameReady,
            this, &AppController::onCaptureFrame, Qt::QueuedConnection);
    connect(m_capture, &EZTranslator::GdiWindowCapture::captureError,
            this, &AppController::onCaptureError, Qt::QueuedConnection);

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

void AppController::start()
{
    m_mainWindow->show();
    m_mainWindow->raise();
    m_mainWindow->activateWindow();
}

void AppController::onStartTranslation()
{
    if (m_mainWindow->selectedWindowHandle() == 0) {
        m_mainWindow->showSelectWindowWarning();
        return;
    }

    m_state = EZTranslator::TranslationState::Running;
    m_mainWindow->onTranslationStarted();
    m_trayManager->updateState(true);

    if (m_capture) {
        m_capture->start(m_mainWindow->selectedWindowHandle());
    }
}

void AppController::onStopTranslation()
{
    m_state = EZTranslator::TranslationState::Idle;
    m_mainWindow->onTranslationStopped();
    m_trayManager->updateState(false);

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
    quintptr handle = m_mainWindow->selectedWindowHandle();
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
    reg.id = QString("roi_%1").arg(m_regions.size() + 1);
    reg.orderNumber = m_regions.size() + 1;
    reg.name = QString("Vùng dịch %1").arg(reg.orderNumber);
    reg.normalizedRect = rect;
    reg.tagColor = QColor(37, 99, 235);

    m_regions.clear();
    m_regions.append(reg);
    m_regionManager.setRegions(m_regions);
    m_currentScreenRect = screenRect;

    if (m_highlightOverlay && m_highlightOverlay->isVisible()) {
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
    if (m_highlightOverlay->isVisible()) {
        m_highlightOverlay->hideOverlay();
        m_mainWindow->setShowRegionActive(false);
        return;
    }

    if (!m_mainWindow->hasRegion()) {
        m_mainWindow->setStatus("Chưa chọn vùng dịch! Bấm 'Chọn vùng (F6)' để quét vùng.", QColor("#f59e0b"));
        return;
    }

    // 1. Ưu tiên tọa độ màn hình thực tế (pixel tuyệt đối) mà người dùng vừa quét
    // Đảm bảo 100% khớp tuyệt đối với vị trí vừa chọn trên màn hình
    QRect screenRect = m_mainWindow->currentScreenRect();
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
            m_capture->start(handle);
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
    m_regions = regions;
    m_regionManager.setRegions(regions);
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

void AppController::onSettingsSaved(const EZTranslator::AppSettings& settings)
{
    m_settings = settings;
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
            m_capture->start(handle);
        }
    }
}

void AppController::onCaptureFrame(const EZTranslator::CapturedFrame& frame)
{
    if (m_regionManager.isEmpty()) {
        // Chưa có region nào – frame đi thẳng vào pipeline khi có detection sau này
        return;
    }

    const QList<EZTranslator::RegionFrame> regionFrames = m_regionManager.extractRegions(frame);

    // Cập nhật lên cửa sổ Preview ROI nếu người dùng đang mở
    if (m_roiPreviewDialog && m_roiPreviewDialog->isVisible()) {
        m_roiPreviewDialog->updateFrames(regionFrames);
    }

    // TODO (task detection/): gửi regionFrames vào ChangeDetector
    // Hiện tại chỉ log để xác nhận pipeline hoạt động
    if (!regionFrames.isEmpty()) {
        qDebug() << "[RegionManager] Extracted" << regionFrames.size() << "RegionFrame(s)"
                 << "from frame" << frame.image.width() << "x" << frame.image.height()
                 << "ts=" << frame.timestamp;
        for (const auto& rf : regionFrames) {
            qDebug() << "  [ROI]" << rf.regionId << rf.regionName
                     << "pixel:" << rf.pixelRect
                     << "crop:" << rf.image.width() << "x" << rf.image.height();
        }
    }
}

void AppController::onCaptureError(EZTranslator::CaptureError error, const QString& detail)
{
    Q_UNUSED(detail)
    using CE = EZTranslator::CaptureError;
    switch (error) {
    case CE::WindowClosed:
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
