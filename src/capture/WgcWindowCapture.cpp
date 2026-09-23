#include "capture/WgcWindowCapture.h"
#include "capture/WgcSharedMemory.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstring>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QProcess>
#include <QStringList>
#include <QThread>
#include <QTimer>

namespace EZTranslator {

namespace {

QString helperPath()
{
    return QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("wgc_capture_helper.exe"));
}

QString controlName(DWORD pid)
{
    return QStringLiteral("Local\\EZTranslator.WGC.control.%1").arg(pid);
}

QString dataName(DWORD pid)
{
    return QStringLiteral("Local\\EZTranslator.WGC.data.%1").arg(pid);
}

const wchar_t* wideName(const QString& name)
{
    return reinterpret_cast<const wchar_t*>(name.utf16());
}

} // namespace

// ============================================================================
//  WgcCaptureWorker – sống trên m_thread, spawn helper + đọc shared memory
// ============================================================================
class WgcCaptureWorker : public QObject
{
    Q_OBJECT
public:
    explicit WgcCaptureWorker(QObject* parent = nullptr) : QObject(parent) {}

public slots:
    void init(int fps)
    {
        m_timer = new QTimer(this);
        m_timer->setInterval(fps > 0 ? 1000 / fps : 16);
        connect(m_timer, &QTimer::timeout, this, &WgcCaptureWorker::poll);
    }

    bool startCapture(quintptr handle)
    {
        cleanup();
        m_stopping = false;
        m_lastSequence = 0;

        if (!m_timer)
            init(60);

        const DWORD pid = GetCurrentProcessId();
        const QString control = controlName(pid);
        const QString data = dataName(pid);

        m_controlHandle = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                             sizeof(wgc::ControlBlock), wideName(control));
        if (!m_controlHandle)
            return false;

        m_control = static_cast<wgc::ControlBlock*>(
            MapViewOfFile(m_controlHandle, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(wgc::ControlBlock)));
        if (!m_control) {
            cleanup();
            return false;
        }
        std::memset(m_control, 0, sizeof(wgc::ControlBlock));
        m_control->magic = wgc::kMagic;
        m_control->version = wgc::kVersion;

        m_process = new QProcess(this);
        connect(m_process, &QProcess::finished, this, &WgcCaptureWorker::onProcessFinished);
        connect(m_process, &QProcess::errorOccurred, this, &WgcCaptureWorker::onProcessError);

        const QStringList args{
            QStringLiteral("--hwnd"), QString::number(handle, 16),
            QStringLiteral("--control"), control,
            QStringLiteral("--data"), data,
        };
        m_process->start(helperPath(), args);
        if (!m_process->waitForStarted(3000)) {
            cleanup();
            return false;
        }

        // Chờ helper tạo xong vùng dữ liệu và báo ready.
        bool ready = false;
        for (int attempt = 0; attempt < 500; ++attempt) {
            if (InterlockedCompareExchange(
                    reinterpret_cast<volatile LONG*>(&m_control->ready), 0, 0) == 1) {
                ready = true;
                break;
            }
            if (m_process->state() == QProcess::NotRunning)
                break;
            QThread::msleep(10);
        }
        if (!ready) {
            cleanup();
            return false;
        }

        m_dataHandle = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, wideName(data));
        if (!m_dataHandle) {
            cleanup();
            return false;
        }
        m_header = static_cast<wgc::FrameHeader*>(
            MapViewOfFile(m_dataHandle, FILE_MAP_ALL_ACCESS, 0, 0, 0));
        if (!m_header || m_header->magic != wgc::kMagic || m_header->slotBytes <= 0) {
            cleanup();
            return false;
        }
        m_dataBase = reinterpret_cast<uint8_t*>(m_header) + sizeof(wgc::FrameHeader);
        m_lastSequence = m_header->sequence;

        m_timer->start();
        return true;
    }

    void stopCapture()
    {
        m_stopping = true;
        if (m_timer)
            m_timer->stop();

        if (m_control)
            InterlockedExchange(reinterpret_cast<volatile LONG*>(&m_control->stop), 1);

        if (m_process && m_process->state() != QProcess::NotRunning) {
            if (!m_process->waitForFinished(500)) {
                m_process->kill();
                m_process->waitForFinished(500);
            }
        }
        cleanup();
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
    void poll()
    {
        if (!m_header || !m_dataBase)
            return;

        const qint64 sequence = InterlockedCompareExchange64(
            reinterpret_cast<volatile LONG64*>(&m_header->sequence), 0, 0);
        if (sequence == m_lastSequence)
            return;

        const int slot = m_header->slot;
        const int width = m_header->width;
        const int height = m_header->height;
        const int stride = m_header->stride;
        const int slotBytes = m_header->slotBytes;
        if (width <= 0 || height <= 0 || stride < width * 4
            || slot < 0 || slot >= wgc::kSlotCount
            || size_t(stride) * size_t(height) > size_t(slotBytes)) {
            return;
        }

        const uint8_t* source = m_dataBase + size_t(slot) * size_t(slotBytes);
        QImage image(width, height, QImage::Format_RGB32);
        if (image.isNull())
            return;
        for (int y = 0; y < height; ++y) {
            std::memcpy(image.scanLine(y), source + size_t(y) * size_t(stride),
                        size_t(width) * 4u);
        }

        m_lastSequence = sequence;

        CapturedFrame frame;
        frame.image = std::move(image);
        frame.sourceSize = frame.image.size();
        frame.timestamp = QDateTime::currentMSecsSinceEpoch();
        emit frameReady(frame);
    }

    void onProcessFinished(int exitCode, QProcess::ExitStatus)
    {
        if (m_stopping)
            return;
        if (m_timer)
            m_timer->stop();
        emit captureError(CaptureError::WindowClosed,
                          QStringLiteral("WGC helper exited (%1)").arg(exitCode));
    }

    void onProcessError(QProcess::ProcessError)
    {
        if (m_stopping)
            return;
        if (m_timer)
            m_timer->stop();
        emit captureError(CaptureError::CaptureFailed,
                          QStringLiteral("WGC helper could not be started"));
    }

private:
    void cleanup()
    {
        if (m_timer)
            m_timer->stop();

        if (m_header) {
            UnmapViewOfFile(m_header);
            m_header = nullptr;
            m_dataBase = nullptr;
        }
        if (m_dataHandle) {
            CloseHandle(m_dataHandle);
            m_dataHandle = nullptr;
        }
        if (m_control) {
            UnmapViewOfFile(m_control);
            m_control = nullptr;
        }
        if (m_controlHandle) {
            CloseHandle(m_controlHandle);
            m_controlHandle = nullptr;
        }
        if (m_process) {
            m_process->disconnect(this);
            m_process->deleteLater();
            m_process = nullptr;
        }
    }

    QTimer*            m_timer{nullptr};
    QProcess*          m_process{nullptr};
    HANDLE             m_controlHandle{nullptr};
    wgc::ControlBlock* m_control{nullptr};
    HANDLE             m_dataHandle{nullptr};
    wgc::FrameHeader*  m_header{nullptr};
    uint8_t*           m_dataBase{nullptr};
    qint64             m_lastSequence{0};
    bool               m_stopping{false};
};

// ============================================================================
//  WgcWindowCapture
// ============================================================================
WgcWindowCapture::WgcWindowCapture(QObject* parent)
    : IWindowCapture(parent)
    , m_thread(new QThread(this))
    , m_worker(new WgcCaptureWorker())
{
    m_worker->moveToThread(m_thread);

    connect(m_thread, &QThread::started, m_worker, [this]() { m_worker->init(m_fps); });

    connect(m_worker, &WgcCaptureWorker::frameReady,
            this, &WgcWindowCapture::frameReady, Qt::QueuedConnection);
    // Chỉ chuyển tiếp lỗi khi đang chạy — lỗi trong lúc start() (để fallback
    // sang GDI) không nên làm phiền UI.
    connect(m_worker, &WgcCaptureWorker::captureError, this,
            [this](CaptureError error, const QString& detail) {
                if (m_running)
                    emit captureError(error, detail);
            });

    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread->start();
}

WgcWindowCapture::~WgcWindowCapture()
{
    stop();
    m_thread->quit();
    m_thread->wait(3000);
}

bool WgcWindowCapture::start(quintptr windowHandle)
{
    if (windowHandle == 0) {
        emit captureError(CaptureError::InvalidWindow, QStringLiteral("windowHandle is 0"));
        return false;
    }

    bool ok = false;
    QMetaObject::invokeMethod(m_worker, "startCapture", Qt::BlockingQueuedConnection,
                              Q_RETURN_ARG(bool, ok), Q_ARG(quintptr, windowHandle));
    m_running = ok;
    return ok;
}

void WgcWindowCapture::stop()
{
    m_running = false;
    QMetaObject::invokeMethod(m_worker, "stopCapture", Qt::BlockingQueuedConnection);
}

bool WgcWindowCapture::isRunning() const
{
    return m_running;
}

void WgcWindowCapture::setFps(int fps)
{
    m_fps = qBound(1, fps, 240);
    QMetaObject::invokeMethod(m_worker, "updateFps", Qt::QueuedConnection, Q_ARG(int, m_fps));
}

} // namespace EZTranslator

// Cần cho Q_OBJECT trong .cpp (AUTOMOC sinh WgcWindowCapture.moc)
#include "WgcWindowCapture.moc"
