# capture/

Chụp nội dung cửa sổ đích. Hai backend, cùng implement `IWindowCapture`:

- **`WgcWindowCapture`** — Windows.Graphics.Capture (WGC), ưu tiên dùng.
- **`GdiWindowCapture`** — GDI `PrintWindow`, fallback khi WGC không start được.

`AppController::startCapture()` thử WGC trước, thất bại thì rơi về GDI.

## Kiến trúc WGC (helper process + shared memory)

Gọi COM interop của WGC trực tiếp trong app Qt có thể crash trên một số máy
(xem `wgc/WgcCapture.h`), nên capture chạy ở **process riêng**:

```
EZTranslator (Qt)                      wgc_capture_helper.exe (Qt-free)
  WgcWindowCapture
    └─ WgcCaptureWorker ── spawn ───►  WgcCapture (WGC)
         │                              │ grab() full window
         │                              │ crop về client area
         │                              ▼
         └── poll ────────────────── [shared memory]  double-buffer
              sequence/slot            ControlBlock: ready/status/stop
                                       FrameHeader + slot0 + slot1
```

- App chính tạo vùng `ControlBlock`, spawn helper, chờ `ready`.
- Helper tạo vùng `FrameHeader` (đúng kích thước cửa sổ), ghi frame mới nhất vào
  slot luân phiên rồi tăng `sequence`.
- App chính poll theo `fps`; chỉ copy + phát `frameReady` khi `sequence` đổi
  (màn hình tĩnh → không tốn CPU).

Layout bộ nhớ chia sẻ nằm ở `WgcSharedMemory.h` (thuần C++, cả hai phía dùng).

## File

| File | Vai trò |
|---|---|
| `IWindowCapture.h` | Interface + `CapturedFrame` / `CaptureError` |
| `WgcWindowCapture.*` | Backend WGC phía Qt (quản lý helper) |
| `WgcSharedMemory.h` | Layout shared memory |
| `wgc/WgcCapture.*` | Wrapper WGC (Qt-free, chỉ dùng trong helper) |
| `GdiWindowCapture.*` | Backend GDI fallback |
| `tools/wgc_capture_helper.cpp` | Helper process (chỉ build trên Windows) |

## Kiểm tra nhanh

```
build\Release\wgc_capture_helper.exe --selftest --hwnd 0x<HWND>
```
Chụp thử một frame từ cửa sổ và in kích thước ra stdout.
