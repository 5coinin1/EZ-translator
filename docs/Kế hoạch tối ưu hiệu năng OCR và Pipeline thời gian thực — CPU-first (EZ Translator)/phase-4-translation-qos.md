# Báo cáo Phase 4 — Translation QoS

## Trạng thái

**Đạt**

Đã triển khai QoS cho pipeline dịch: mailbox giới hạn request vùng, ưu tiên hover, deadline tổng cho online translation và circuit breaker có backoff cho Google khi bị rate-limit hoặc lỗi mạng.

## Phạm vi

Ngày triển khai: 2026-10-23

Mục tiêu:

1. Không để các request vùng cũ tích tụ trong event queue.
2. Cho request hover có độ trễ thấp và có thể chen vào lượt dịch vùng đang chạy.
3. Giới hạn thời gian chờ online theo cả từng request và toàn bộ batch.
4. Khi Google trả `429` hoặc lỗi mạng lặp lại, tạm ngắt request trong một khoảng backoff để fallback có cơ hội xử lý.
5. Giữ luật bỏ kết quả cũ bằng token hiện có ở `AppController`.

## Thay đổi đã thực hiện

### Mailbox và ưu tiên

`TranslationWorker` không xử lý trực tiếp mọi queued slot nữa.

- Request vùng được gộp theo `regionId`; cùng một vùng chỉ giữ text/token mới nhất.
- Nhiều vùng khác nhau được lấy theo round-robin để một vùng không chiếm toàn bộ worker.
- Hover được giữ riêng trong một ô latest-wins và được xử lý trước request vùng.
- Khi worker đang xử lý vùng, `processEvents()` giữa các chunk cho phép hover chen vào; các hover tiếp theo vẫn chỉ giữ bản mới nhất.
- Thời gian chờ từ lúc AppController gửi request được log qua `queueMs`.

Điều này giới hạn số request chờ trong worker theo số vùng đang hoạt động, thay vì tăng không giới hạn theo số frame OCR.

### Deadline online

- Google endpoint miễn phí giữ timeout tối đa 4 giây cho từng dòng và 6 giây cho toàn batch.
- MyMemory giữ timeout tối đa 4 giây cho từng dòng và 6 giây cho toàn batch.
- Google Cloud API giữ timeout 10 giây cho một request batch.
- Batch bị thiếu dòng sau deadline vẫn trả danh sách rỗng đúng vị trí để `TranslationWorker` chuyển phần thiếu sang fallback.

### Circuit breaker Google

Thêm `TranslationCircuitBreaker` độc lập:

- Lỗi mạng/timeout: cooldown ngắn mặc định 2 giây.
- `429`: backoff tăng dần 15, 30, 60, 120 giây, giới hạn 120 giây.
- Có thể dùng thời gian `Retry-After` hợp lệ từ server nếu được truyền vào policy.
- Response thành công đóng circuit và xoá chuỗi rate-limit.
- Khi circuit mở, Google bỏ qua request mới và để chuỗi engine thử fallback, tránh spam endpoint.

### Quan sát runtime

Worker ghi thêm:

`[TRANSLATE_METRIC] id=... interactive=... queueMs=... totalMs=... new=... complete=...`

Log này tách được thời gian xếp hàng khỏi thời gian dịch thực tế, giúp kiểm tra tình trạng hover bị chờ bởi region hoặc online API bị treo.

## Kiểm tra

- `test_translation_qos.exe`: pass 3 test về cooldown lỗi mạng, backoff `429`/`Retry-After` và đóng circuit sau thành công.
- `test_ocr_mailbox.exe`: pass.
- `test_ocr_thread_budget.exe`: pass.
- `ctest --test-dir build --output-on-failure`: **100% pass, 3/3 test**.
- `ez_translate`, `ez_ocr`, `ocr_bench` build thành công.
- `AppController.cpp` compile thành công trong target `EZTranslator`.
- Link toàn bộ `EZTranslator.exe` chưa hoàn tất vì instance ứng dụng đang chạy giữ lock trên `build/EZTranslator.exe`; đây là giới hạn môi trường build, không phải lỗi compile của Phase 4.

## Chưa thể kết luận từ test tự động

- Chưa đo p95 trên game thật với mạng chập chờn và phản hồi `429` thực tế.
- Chưa kiểm tra `Retry-After` từ từng provider vì test không gọi mạng thật.
- Fallback MyMemory vẫn tuần tự theo từng dòng; deadline tổng đã chặn thời gian tối đa nhưng chất lượng/tốc độ thực tế cần đo thêm trên cấu hình người dùng.
- Không thay đổi cơ chế token ở `AppController`; kết quả cũ vẫn bị loại như trước.

## Rủi ro và rollback

- Mailbox mới làm mất các request vùng trung gian, đúng theo latest-wins; nếu cần lưu mọi request phục vụ audit thì phải dùng queue riêng, không phù hợp đường realtime.
- Circuit breaker có thể trì hoãn Google sau một đợt lỗi ngắn; fallback vẫn được gọi trong chuỗi engine.
- Rollback Phase 4: revert các thay đổi trong `TranslationWorker.*`, `GoogleTranslator.cpp`, `MyMemoryTranslator.cpp`, `TranslationCircuitBreaker.*`, test và CMake/report Phase 4.

## Kết luận

Phase 4 đã hoàn tất phần QoS trong code và test tự động. Pipeline dịch hiện có giới hạn queue, ưu tiên tương tác, deadline online và backoff chống retry storm; bước còn lại là validation p95 trên game thật và mạng thật.
