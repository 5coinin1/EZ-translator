#include "capture/GdiWindowCapture.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

// PrintWindow flags (Win8.1+)
#ifndef PW_CLIENTONLY
#define PW_CLIENTONLY 0x00000001
#endif
#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif

#include <QThread>
#include <QTimer>
#include <QDateTime>
#include <QDebug>

namespace EZTranslator {

// ============================================================================
//  CaptureWorker – lives on m_thread, performs actual GDI capture
// ============================================================================
class CaptureWorker : public QObject
{
    Q_OBJECT
public:
    explicit CaptureWorker(QObject* parent = nullptr) : QObject(parent) {}

public slots:
    void init(int fps)
    {
        m_timer = new QTimer(this);
        m_timer->setInterval(fps > 0 ? 1000 / fps : 33);
        connect(m_timer, &QTimer::timeout, this, &CaptureWorker::doCapture);
    }

    void startCapture(quintptr handle)
    {
        m_hwnd = reinterpret_cast<HWND>(handle);
        if (m_timer) m_timer->start();
    }

    void stopCapture()
    {
        if (m_timer) m_timer->stop();
        m_hwnd = nullptr;
    }

    void updateFps(int fps)
    {
        if (m_timer && fps > 0)
            m_timer->setInterval(1000 / fps);
    }

signals:
    void frameReady(const EZTranslator::CapturedFrame& frame);
    void captureError(EZTranslator::CaptureError error, const QString& detail);

private slots:
    void doCapture()
    {
        if (!m_hwnd) return;

        // Window no longer exists
        if (!IsWindow(m_hwnd)) {
            m_timer->stop();
            m_hwnd = nullptr;
            emit captureError(CaptureError::WindowClosed, QStringLiteral("Window handle invalid"));
            return;
        }

        // Window minimized – emit warning but keep timer running (may be restored)
        if (IsIconic(m_hwnd)) {
            emit captureError(CaptureError::WindowMinimized, QStringLiteral("Window is minimized"));
            return;
        }

        // Get client area size
        RECT rc = {};
        if (!GetClientRect(m_hwnd, &rc)) {
            emit captureError(CaptureError::CaptureFailed, QStringLiteral("GetClientRect failed"));
            return;
        }
        int w = rc.right  - rc.left;
        int h = rc.bottom - rc.top;
        if (w <= 0 || h <= 0) return;

        // ── GDI capture ──────────────────────────────────────────────────────
        HDC hdcSrc = GetDC(m_hwnd);
        if (!hdcSrc) {
            emit captureError(CaptureError::InitializationFailed, QStringLiteral("GetDC failed"));
            return;
        }

        HDC hdcMem = CreateCompatibleDC(hdcSrc);
        if (!hdcMem) {
            ReleaseDC(m_hwnd, hdcSrc);
            emit captureError(CaptureError::InitializationFailed, QStringLiteral("CreateCompatibleDC failed"));
            return;
        }

        HBITMAP hBmp = CreateCompatibleBitmap(hdcSrc, w, h);
        if (!hBmp) {
            DeleteDC(hdcMem);
            ReleaseDC(m_hwnd, hdcSrc);
            emit captureError(CaptureError::InitializationFailed, QStringLiteral("CreateCompatibleBitmap failed"));
            return;
        }

        HGDIOBJ hOld = SelectObject(hdcMem, hBmp);

        // Chụp chính xác Client Area (loại bỏ title bar và viền cửa sổ)
        BOOL ok = PrintWindow(m_hwnd, hdcMem, PW_CLIENTONLY | PW_RENDERFULLCONTENT);
        if (!ok) {
            ok = PrintWindow(m_hwnd, hdcMem, PW_CLIENTONLY);
        }
        if (!ok) {
            ok = PrintWindow(m_hwnd, hdcMem, PW_RENDERFULLCONTENT);
        }
        if (!ok) {
            // Fallback: BitBlt (hdcSrc từ GetDC(m_hwnd) là Client DC)
            ok = BitBlt(hdcMem, 0, 0, w, h, hdcSrc, 0, 0, SRCCOPY | CAPTUREBLT);
        }

        // ── Convert HBITMAP -> QImage (Format_RGB32 = BGRX, alpha forced 0xFF) ──
        QImage img;
        if (ok) {
            BITMAPINFOHEADER bi = {};
            bi.biSize        = sizeof(bi);
            bi.biWidth       = w;
            bi.biHeight      = -h;  // negative = top-down DIB
            bi.biPlanes      = 1;
            bi.biBitCount    = 32;
            bi.biCompression = BI_RGB;

            // Format_RGB32: memory layout BGRA where A is always 0xFF.
            // GetDIBits with BI_RGB 32bit returns BGRX (X=0).
            // Qt's Format_RGB32 treats the high byte as 0xFF internally.
            img = QImage(w, h, QImage::Format_RGB32);
            if (!img.isNull()) {
                if (!GetDIBits(hdcMem, hBmp, 0, static_cast<UINT>(h),
                               img.bits(),
                               reinterpret_cast<BITMAPINFO*>(&bi),
                               DIB_RGB_COLORS)) {
                    img = QImage{};
                }
            }
        }

        // ── RAII cleanup (always executed) ───────────────────────────────────
        SelectObject(hdcMem, hOld);
        DeleteObject(hBmp);
        DeleteDC(hdcMem);
        ReleaseDC(m_hwnd, hdcSrc);

        if (img.isNull()) {
            emit captureError(CaptureError::CaptureFailed, QStringLiteral("GetDIBits returned empty image"));
            return;
        }

        CapturedFrame frame;
        frame.sourceSize = img.size();
        frame.image      = std::move(img);
        frame.timestamp  = QDateTime::currentMSecsSinceEpoch();
        emit frameReady(frame);
    }

private:
    QTimer* m_timer{nullptr};
    HWND    m_hwnd{nullptr};
};

// ============================================================================
//  GdiWindowCapture
// ============================================================================
GdiWindowCapture::GdiWindowCapture(QObject* parent)
    : IWindowCapture(parent)
    , m_thread(new QThread(this))
    , m_worker(new CaptureWorker())
{
    m_worker->moveToThread(m_thread);

    // Init worker (timer setup) when thread starts
    connect(m_thread, &QThread::started, m_worker, [this]() {
        m_worker->init(m_fps);
    });

    // Forward worker signals to IWindowCapture signals (queued: worker->main thread)
    connect(m_worker, &CaptureWorker::frameReady,
            this,     &GdiWindowCapture::frameReady,
            Qt::QueuedConnection);
    connect(m_worker, &CaptureWorker::captureError,
            this,     &GdiWindowCapture::captureError,
            Qt::QueuedConnection);

    // Auto-stop m_running flag when captureError signals window closed/invalid
    connect(m_worker, &CaptureWorker::captureError,
            this, [this](CaptureError err, const QString&) {
        if (err == CaptureError::WindowClosed || err == CaptureError::InvalidWindow) {
            m_running = false;
        }
    }, Qt::QueuedConnection);

    // Worker cleaned up when thread finishes
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

    m_thread->start();
}

GdiWindowCapture::~GdiWindowCapture()
{
    stop();
    m_thread->quit();
    m_thread->wait(3000); // max 3s grace period
}

bool GdiWindowCapture::start(quintptr windowHandle)
{
    if (windowHandle == 0) {
        emit captureError(CaptureError::InvalidWindow, QStringLiteral("windowHandle is 0"));
        return false;
    }

    // Validate HWND
    HWND hwnd = reinterpret_cast<HWND>(windowHandle);
    if (!IsWindow(hwnd)) {
        emit captureError(CaptureError::InvalidWindow, QStringLiteral("HWND is not a valid window"));
        return false;
    }

    // If already running a different window, stop first
    if (m_running) {
        QMetaObject::invokeMethod(m_worker, "stopCapture", Qt::QueuedConnection);
    }

    m_running = true;
    QMetaObject::invokeMethod(m_worker, "startCapture",
                              Qt::QueuedConnection,
                              Q_ARG(quintptr, windowHandle));
    return true;
}

void GdiWindowCapture::stop()
{
    if (!m_running) return;
    m_running = false;
    QMetaObject::invokeMethod(m_worker, "stopCapture", Qt::QueuedConnection);
}

bool GdiWindowCapture::isRunning() const
{
    return m_running;
}

void GdiWindowCapture::setFps(int fps)
{
    m_fps = qBound(1, fps, 120);
    QMetaObject::invokeMethod(m_worker, "updateFps",
                              Qt::QueuedConnection,
                              Q_ARG(int, m_fps));
}

} // namespace EZTranslator

// Required for Q_OBJECT in .cpp (AUTOMOC will generate GdiWindowCapture.moc)
#include "GdiWindowCapture.moc"
