// Helper process stream frame Windows.Graphics.Capture vào shared memory.
//
// Capture chạy trong process riêng (không Qt) vì gọi COM interop này từ trong app
// Qt có thể nhảy vào vùng nhớ rác trên một số máy, trong khi cùng đoạn code đó
// chạy hoàn hảo trong process thuần.
//
// Usage:
//   wgc_capture_helper.exe --hwnd 0x1234 --control <name> --data <name>
//
// Giao thức: app chính tạo vùng control và chờ `ready`. Helper tạo vùng data,
// ghi liên tục frame mới nhất vào double-buffer rồi tăng `sequence`. App chính
// đọc frame theo `sequence`. Helper thoát khi control.stop = 1, cửa sổ đóng,
// hoặc app chính kill process.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <windows.h>

#include "capture/WgcSharedMemory.h"
#include "capture/wgc/WgcCapture.h"

#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT)-4)
#endif

namespace {

using EZTranslator::WgcCapture;
namespace wgc = EZTranslator::wgc;

/** Bật DPI awareness để GetWindowRect/GetClientRect trả toạ độ physical khớp WGC. */
void enableDpiAwareness()
{
    if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
        using SetContextFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
        auto setContext = reinterpret_cast<SetContextFn>(
            GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (setContext && setContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
            return;
    }
    SetProcessDPIAware();
}

struct CropRect
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    bool valid = false;
};

/** Vùng client (nội dung cần dịch) bên trong frame cả cửa sổ. */
CropRect clientCrop(HWND hwnd, int fullWidth, int fullHeight)
{
    RECT windowRect{};
    RECT clientRect{};
    POINT origin{0, 0};
    if (!GetWindowRect(hwnd, &windowRect) || !GetClientRect(hwnd, &clientRect)
        || !ClientToScreen(hwnd, &origin)) {
        return {};
    }

    int x = origin.x - windowRect.left;
    int y = origin.y - windowRect.top;
    int width = clientRect.right - clientRect.left;
    int height = clientRect.bottom - clientRect.top;
    if (width <= 0 || height <= 0)
        return {};

    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x + width > fullWidth) width = fullWidth - x;
    if (y + height > fullHeight) height = fullHeight - y;
    if (width <= 0 || height <= 0)
        return {};

    return {x, y, width, height, true};
}

void writeFrame(wgc::FrameHeader* header, uint8_t* slots, int slotBytes,
                const WgcCapture::Frame& frame, const CropRect& crop)
{
    const int width = crop.valid ? crop.width : frame.width;
    const int height = crop.valid ? crop.height : frame.height;
    const int x = crop.valid ? crop.x : 0;
    const int y = crop.valid ? crop.y : 0;

    const int slot = (header->slot + 1) % wgc::kSlotCount;
    uint8_t* destination = slots + size_t(slot) * size_t(slotBytes);

    const int rowBytes = width * 4;
    for (int row = 0; row < height; ++row) {
        const uint8_t* source = frame.pixels.data() + size_t(y + row) * size_t(frame.stride)
                                + size_t(x) * 4u;
        std::memcpy(destination + size_t(row) * size_t(rowBytes), source, size_t(rowBytes));
    }

    header->width = width;
    header->height = height;
    header->stride = rowBytes;
    MemoryBarrier();
    InterlockedExchange(reinterpret_cast<volatile LONG*>(&header->slot), LONG(slot));
    InterlockedIncrement64(reinterpret_cast<volatile LONG64*>(&header->sequence));
}

void fail(wgc::ControlBlock* control, wgc::HelperStatus status)
{
    if (control) {
        control->status = static_cast<int32_t>(status);
        InterlockedExchange(reinterpret_cast<volatile LONG*>(&control->ready), -1);
    }
}

} // namespace

int main(int argc, char** argv)
{
    unsigned long long hwndValue = 0;
    const char* controlName = nullptr;
    const char* dataName = nullptr;
    bool selfTest = false;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        const auto next = [&argc, &argv, &i]() -> const char* {
            return (i + 1 < argc) ? argv[++i] : nullptr;
        };
        if (std::strcmp(arg, "--hwnd") == 0) {
            if (const char* value = next())
                hwndValue = std::strtoull(value, nullptr, 0);
        } else if (std::strcmp(arg, "--control") == 0) {
            controlName = next();
        } else if (std::strcmp(arg, "--data") == 0) {
            dataName = next();
        } else if (std::strcmp(arg, "--selftest") == 0) {
            selfTest = true;
        }
    }

    enableDpiAwareness();

    if (selfTest) {
        if (hwndValue == 0)
            return 1;
        WgcCapture capture;
        if (!capture.start(uintptr_t(hwndValue))) {
            std::fprintf(stderr, "selftest: capture start failed\n");
            return 3;
        }
        int frames = 0;
        int lastWidth = 0;
        int lastHeight = 0;
        for (int attempt = 0; attempt < 200 && frames < 10; ++attempt) {
            WgcCapture::Frame frame = capture.grab(0, 0, 1 << 20, 1 << 20);
            if (!frame.isEmpty()) {
                ++frames;
                lastWidth = frame.width;
                lastHeight = frame.height;
                std::printf("selftest: frame %d %dx%d stride=%d\n", frames, frame.width,
                            frame.height, frame.stride);
            } else {
                Sleep(10);
            }
        }
        capture.stop();
        std::printf("selftest: %d frame(s), last %dx%d\n", frames, lastWidth, lastHeight);
        return frames > 0 ? 0 : 2;
    }

    if (hwndValue == 0 || !controlName || !dataName)
        return 1;

    HANDLE controlHandle = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, controlName);
    if (!controlHandle)
        return 2;

    auto* control = static_cast<wgc::ControlBlock*>(
        MapViewOfFile(controlHandle, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(wgc::ControlBlock)));
    if (!control) {
        CloseHandle(controlHandle);
        return 2;
    }

    HWND hwnd = reinterpret_cast<HWND>(hwndValue);
    WgcCapture capture;
    if (!capture.start(uintptr_t(hwndValue))) {
        fail(control, wgc::HelperStatus::CaptureStartFailed);
        UnmapViewOfFile(control);
        CloseHandle(controlHandle);
        return 3;
    }

    // Chờ frame đầu tiên để biết kích thước cửa sổ thật.
    WgcCapture::Frame first;
    for (int attempt = 0; attempt < 300 && first.isEmpty(); ++attempt) {
        first = capture.grab(0, 0, 1 << 20, 1 << 20);
        if (first.isEmpty())
            Sleep(10);
    }
    if (first.isEmpty()) {
        capture.stop();
        fail(control, wgc::HelperStatus::CaptureStartFailed);
        UnmapViewOfFile(control);
        CloseHandle(controlHandle);
        return 3;
    }

    const int allocatedWidth = first.width;
    const int allocatedHeight = first.height;
    const int slotBytes = wgc::slotBytesFor(allocatedWidth, allocatedHeight);
    const size_t dataBytes = sizeof(wgc::FrameHeader)
                             + size_t(wgc::kSlotCount) * size_t(slotBytes);

    HANDLE dataHandle = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                           DWORD(dataBytes >> 32), DWORD(dataBytes & 0xFFFFFFFF),
                                           dataName);
    auto* header = dataHandle
                       ? static_cast<wgc::FrameHeader*>(
                             MapViewOfFile(dataHandle, FILE_MAP_ALL_ACCESS, 0, 0, dataBytes))
                       : nullptr;
    if (!header) {
        capture.stop();
        if (dataHandle) CloseHandle(dataHandle);
        fail(control, wgc::HelperStatus::DataCreateFailed);
        UnmapViewOfFile(control);
        CloseHandle(controlHandle);
        return 4;
    }

    std::memset(header, 0, sizeof(wgc::FrameHeader));
    header->magic = wgc::kMagic;
    header->version = wgc::kVersion;
    header->slotCount = wgc::kSlotCount;
    header->slotBytes = slotBytes;
    header->width = 0;
    header->height = 0;
    header->stride = 0;
    header->slot = 0;
    header->sequence = 0;
    auto* slots = reinterpret_cast<uint8_t*>(header) + sizeof(wgc::FrameHeader);

    // Công bố frame đầu tiên rồi báo sẵn sàng.
    writeFrame(header, slots, slotBytes, first, clientCrop(hwnd, first.width, first.height));
    control->status = static_cast<int32_t>(wgc::HelperStatus::Ok);
    InterlockedExchange(reinterpret_cast<volatile LONG*>(&control->ready), 1);

    while (true) {
        if (InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(&control->stop), 0, 0) != 0)
            break;
        if (!IsWindow(hwnd)) {
            control->status = static_cast<int32_t>(wgc::HelperStatus::WindowClosed);
            break;
        }

        WgcCapture::Frame frame = capture.grab(0, 0, 1 << 20, 1 << 20);
        if (frame.isEmpty()) {
            Sleep(5);
            continue;
        }

        CropRect crop = clientCrop(hwnd, frame.width, frame.height);
        if (crop.valid) {
            // Frame có thể to hơn vùng đã cấp phát nếu cửa sổ bị phóng to.
            crop.width = std::min(crop.width, allocatedWidth);
            crop.height = std::min(crop.height, allocatedHeight);
            crop.x = std::min(crop.x, frame.width - crop.width);
            crop.y = std::min(crop.y, frame.height - crop.height);
            if (crop.x < 0) crop.x = 0;
            if (crop.y < 0) crop.y = 0;
        }
        writeFrame(header, slots, slotBytes, frame, crop);
        Sleep(1);
    }

    capture.stop();
    UnmapViewOfFile(header);
    CloseHandle(dataHandle);
    UnmapViewOfFile(control);
    CloseHandle(controlHandle);
    return 0;
}
