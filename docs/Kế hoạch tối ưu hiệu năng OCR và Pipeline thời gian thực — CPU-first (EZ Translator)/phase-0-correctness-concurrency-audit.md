# Báo cáo Phase 0 — Correctness & Concurrency Audit

## Trạng thái

**Đạt một phần**

Phase 0 đã triển khai hai thay đổi correctness quan trọng. Các bài stress test concurrency, stale lifecycle đầy đủ và kiểm tra bằng ThreadSanitizer chưa được bổ sung trong lần này.

## Phạm vi

Ngày triển khai: 2026-10-23

Mục tiêu đã thực hiện:

1. Loại bỏ ghi `LineCache` đồng thời từ các recognition threads.
2. Gắn `requestToken` và `imageHash` vào kết quả OCR region và smart-cover plate.
3. Bỏ qua kết quả OCR/plate nếu không còn thuộc request mới nhất của region.

## Thay đổi đã thực hiện

### 1. Bảo vệ line cache khỏi concurrent writes

Trong đường CPU parallel recognition, mỗi thread vẫn ghi kết quả vào vị trí riêng trong `outputs` và `have`. Việc ghi `QHash/QQueue` của `LineCache` được dời đến sau khi toàn bộ thread đã `join`.

File chính:

- `src/ocr/OcrRecognizer.cpp`

Kết quả: tránh nhiều thread cùng gọi `LineCache::insert()` trên cấu trúc Qt không hỗ trợ ghi đồng thời.

### 2. Gắn request context cho region OCR

`OcrWorker::recognizeRegion()` và các signal liên quan hiện truyền thêm:

- `requestToken`: nhận diện request mới nhất của region.
- `imageHash`: xác nhận nội dung ảnh ROI tương ứng.

Các file thay đổi:

- `src/ocr/OcrWorker.h`
- `src/ocr/OcrWorker.cpp`
- `src/app/AppController.h`
- `src/app/AppController.cpp`

### 3. Loại bỏ kết quả stale

`AppController` kiểm tra token/hash hiện tại trước khi:

- Gắn box OCR vào overlay.
- Lưu hoặc áp dụng smart-cover plate.

Kết quả từ request cũ bị bỏ qua và không được dùng với geometry/frame mới.

## Kiểm tra

- `cmake --build build --target ez_ocr -j 4`: đạt.
- Các file `OcrRecognizer.cpp`, `OcrWorker.cpp`, `AppController.cpp`: compile đạt.
- `git diff --check`: đạt, chỉ có cảnh báo chuyển đổi LF/CRLF của Git.
- Link `EZTranslator.exe`: chưa hoàn tất vì executable đang chạy và bị Windows khóa (`Permission denied`).
- Chưa chạy benchmark nặng.
- Chưa có stress test riêng cho 50 request hover/region.
- Chưa chạy ThreadSanitizer.

## Phần chưa hoàn tất của Phase 0

- Thêm test race/stress cho parallel recognition.
- Kiểm tra stop/start, đổi cửa sổ và đổi display mode trong lúc OCR đang chạy.
- Kiểm tra stale translation và stale overlay xuyên suốt toàn pipeline.
- Xác nhận bounded mailbox của hover; nội dung này thuộc Phase 2.

## Rủi ro và rollback

Rủi ro còn lại là logic coalescing/backpressure chưa được triển khai, nên queued request vẫn có thể tồn tại dù kết quả stale đã bị lọc. Nếu phát sinh regression, có thể rollback phần truyền request context độc lập với phần sửa ghi cache sau `join`.

## File thay đổi trong lần triển khai

- `src/ocr/OcrRecognizer.cpp`
- `src/ocr/OcrWorker.h`
- `src/ocr/OcrWorker.cpp`
- `src/app/AppController.h`
- `src/app/AppController.cpp`
- `docs/ocr-performance-optimization-plan.md`

## Kết luận

Hai lỗi correctness có mức ưu tiên P0 đã được xử lý ở mức implementation cơ bản: cache không còn được ghi từ nhiều recognition threads cùng lúc, và kết quả region cũ không còn được áp trực tiếp lên state mới. Phase 0 chỉ được coi là hoàn tất toàn bộ sau khi bổ sung stress/lifecycle tests và xác nhận build/link trên executable không bị khóa.
