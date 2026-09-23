#pragma once

#include <cstdint>
#include <vector>

namespace EZTranslator {

/**
 * @brief Wrapper cho Windows.Graphics.Capture (Windows 10 1903+).
 *
 * Đây là API capture duy nhất trên Windows lấy được MỌI cửa sổ GUI — kể cả
 * cửa sổ DirectX / D3D11 / D3D12 borderless — và vẫn hoạt động khi cửa sổ bị
 * che một phần hoặc không phải cửa sổ foreground. Khác PrintWindow (đường
 * message hay hỏng với DX) và khác Desktop Duplication (chỉ theo màn hình).
 *
 * QUAN TRỌNG: giữ translation unit này KHÔNG dùng Qt. Capture chạy trong một
 * helper process (tools/wgc_capture_helper.cpp) vì gọi COM interop này từ bên
 * trong app Qt có thể nhảy vào vùng nhớ rác trên một số máy.
 *
 * start()/grab()/stop() phải gọi trên CÙNG một thread.
 */
class WgcCapture
{
public:
    /** Một frame đã chụp: BGRA8, top-down, `stride` byte mỗi hàng. */
    struct Frame
    {
        int width = 0;
        int height = 0;
        int stride = 0;
        std::vector<uint8_t> pixels;

        [[nodiscard]] bool isEmpty() const { return pixels.empty() || width <= 0 || height <= 0; }
    };

    WgcCapture();
    ~WgcCapture();

    WgcCapture(const WgcCapture&) = delete;
    WgcCapture& operator=(const WgcCapture&) = delete;

    /** True khi OS hỗ trợ Windows.Graphics.Capture (Windows 10 1903+). */
    [[nodiscard]] static bool isSupported();

    /** Bắt đầu capture cửa sổ `windowId`. Trả false nếu không hỗ trợ/bị từ chối. */
    bool start(uintptr_t windowId);
    [[nodiscard]] bool isValid() const;
    void stop();

    /**
     * Lấy frame mới nhất đã crop về (x, y, width, height) trong toạ độ *cửa sổ*
     * (capture item bao trọn cả title bar và viền). Frame rỗng nghĩa là chưa có
     * frame mới (cửa sổ chưa vẽ lại) — gọi lại ở tick sau.
     */
    Frame grab(int x, int y, int width, int height);

    /** grab() lặp tới khi có frame hoặc hết `timeoutMs` (blocking). */
    Frame grabWait(int x, int y, int width, int height, int timeoutMs);

private:
    struct Impl;
    Impl* m_impl = nullptr;
};

} // namespace EZTranslator
