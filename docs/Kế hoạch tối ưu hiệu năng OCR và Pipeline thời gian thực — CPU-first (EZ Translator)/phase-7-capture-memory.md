# Báo cáo Phase 7 — Capture & Memory

## Trạng thái

**Đạt**

Đã bổ sung quan sát runtime cho backend capture và xác nhận backpressure/các cache giới hạn đang được áp dụng.

## Đã làm

- Log rõ lần WGC start thất bại trước khi chuyển GDI.
- Log lỗi capture kèm mã và chi tiết qua `[CAPTURE_ERROR]`.
- Log định kỳ `[CAPTURE_METRIC]` gồm backend thật, số frame, kích thước ảnh, bytes ảnh và FPS cấu hình.
- Giữ `framePending`/`notifyFrameConsumed()` để không tích tụ `QImage` full-screen trong Qt event queue.
- Giữ giới hạn plate cache theo vùng và tổng 48 MiB; box/region/hover mailbox vẫn bounded.
- WGC fallback sau 1,5 giây không có frame vẫn giữ lý do trong log.

## Kiểm tra

- `EZTranslator` compile toàn bộ `AppController.cpp` và capture source.
- `ez_ocr`, `ocr_bench` và các test build thành công.
- CTest pass `3/3`.
- Link app cuối bị Windows khóa bởi instance `build/EZTranslator.exe` đang chạy; cần đóng app rồi build lại để tạo executable mới.

## Giới hạn

Chưa có số đo working-set nhiều giờ trên WGC/GDI và nhiều vùng 4K. Log mới đủ để thu thập dữ liệu thật mà không tự khẳng định capture backend nào nhanh hơn.

