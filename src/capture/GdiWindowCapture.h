#pragma once
#include "capture/IWindowCapture.h"

class QThread;

namespace EZTranslator {

class CaptureWorker; // forward – defined in .cpp

/**
 * @brief Capture implementation dung GDI PrintWindow API.
 *
 * Chay tren worker thread rieng de khong block GUI.
 * PrintWindow() lay noi dung truc tiep tu HWND, khong screenshot toan desktop.
 *
 * Tuong thich: Windows 10/11, MinGW-w64, khong can WinRT.
 *
 * Resource lifecycle:
 *   start(handle) -> worker thread chay -> timer tick -> doCapture()
 *   stop()        -> timer stop -> thread quit -> cleanup HDC/HBITMAP
 */
class GdiWindowCapture : public IWindowCapture
{
    Q_OBJECT
public:
    explicit GdiWindowCapture(QObject* parent = nullptr);
    ~GdiWindowCapture() override;

    bool start(quintptr windowHandle) override;
    void stop() override;
    [[nodiscard]] bool isRunning() const override;
    [[nodiscard]] int  fps() const override { return m_fps; }
    void setFps(int fps) override;

private:
    QThread*       m_thread{nullptr};
    CaptureWorker* m_worker{nullptr};
    int            m_fps{30};
    bool           m_running{false};
};

} // namespace EZTranslator
