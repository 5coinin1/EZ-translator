#pragma once

#include <QObject>
#include <QImage>
#include <QSize>

namespace EZTranslator {

/**
 * @brief Frame data tra ve tu capture module.
 * sourceSize giu kich thuoc goc cua window de ROI pipeline tinh toan sau nay.
 */
struct CapturedFrame {
    QImage  image;        ///< Anh chup window (ARGB32)
    QSize   sourceSize;   ///< Kich thuoc goc truoc khi bat ky scaling nao
    qint64  timestamp;    ///< QDateTime::currentMSecsSinceEpoch()
};

/**
 * @brief Error codes cua capture module - khong de Win32 error code len UI.
 */
enum class CaptureError {
    None,
    InvalidWindow,        ///< Handle khong hop le luc start
    WindowClosed,         ///< Window bien mat trong luc capture
    WindowMinimized,      ///< Window dang bi thu nho
    PermissionDenied,     ///< Khong duoc phep capture (UWP, protected content...)
    InitializationFailed, ///< Khong khoi tao duoc DC/bitmap
    CaptureFailed         ///< PrintWindow/BitBlt tra false trong runtime
};

/**
 * @brief Interface thuan ao cho capture implementation.
 *
 * MainWindow va AppController CHI thao tac qua interface nay,
 * khong phu thuoc truc tiep vao GDI/WinRT/D3D types.
 *
 * Lifecycle:
 *   start(handle) -> [frameReady x N] -> stop()
 *
 * Signal frameReady duoc emit tu worker thread -> phai dung
 * Qt::QueuedConnection khi connect voi UI slot.
 */
class IWindowCapture : public QObject
{
    Q_OBJECT
public:
    explicit IWindowCapture(QObject* parent = nullptr) : QObject(parent) {}
    ~IWindowCapture() override = default;

    virtual bool start(quintptr windowHandle) = 0;
    virtual void stop() = 0;
    [[nodiscard]] virtual bool isRunning() const = 0;
    [[nodiscard]] virtual int fps() const = 0;
    virtual void setFps(int fps) = 0;

signals:
    void frameReady(const EZTranslator::CapturedFrame& frame);
    void captureError(EZTranslator::CaptureError error, const QString& detail);
};

} // namespace EZTranslator
