#pragma once
#include "capture/IWindowCapture.h"

class QThread;

namespace EZTranslator {

class WgcCaptureWorker; // forward – định nghĩa trong .cpp

/**
 * @brief Capture bằng Windows.Graphics.Capture qua helper process.
 *
 * Vì gọi COM interop của WGC trực tiếp trong app Qt có thể crash trên một số
 * máy, capture được đẩy sang một process riêng (`wgc_capture_helper.exe`) và
 * truyền frame về qua shared memory. Lớp này chỉ:
 *   - spawn/kill helper trong start()/stop()
 *   - poll shared memory theo fps và phát frameReady
 *
 * Chạy trên worker thread riêng để không block GUI.
 */
class WgcWindowCapture : public IWindowCapture
{
    Q_OBJECT
public:
    explicit WgcWindowCapture(QObject* parent = nullptr);
    ~WgcWindowCapture() override;

    bool start(quintptr windowHandle) override;
    void stop() override;
    [[nodiscard]] bool isRunning() const override;
    [[nodiscard]] int  fps() const override { return m_fps; }
    void setFps(int fps) override;

private:
    QThread*          m_thread{nullptr};
    WgcCaptureWorker* m_worker{nullptr};
    int               m_fps{60};
    bool              m_running{false};
};

} // namespace EZTranslator
