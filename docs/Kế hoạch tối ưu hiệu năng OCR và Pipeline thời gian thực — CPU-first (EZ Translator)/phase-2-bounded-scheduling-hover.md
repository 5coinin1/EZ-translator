# Báo cáo Phase 2 — Bounded Scheduling & Latest-wins cho Hover và Region

## Trạng thái

**Đạt**

Đã hoàn tất bounded mailbox cho hover OCR và mailbox latest-wins có round-robin cho OCR vùng. Các kiểm thử burst, fairness và lifecycle đều đã chạy đạt.

## Phạm vi

Ngày triển khai: 2026-10-23

Mục tiêu đã thực hiện:

1. Giới hạn hover ở tối đa một request đang chạy.
2. Giữ tối đa một request mới nhất đang chờ.
3. Cho probe chính thức thay probe prefetch đang chờ.
4. Giới hạn mỗi region ở một request đang chạy và một request mới nhất đang chờ.
5. Không để trạng thái worker cũ tồn tại sau stop/reload hoặc đổi ngôn ngữ.

## Thay đổi đã thực hiện

### Hover mailbox

Thêm `m_hoverProbeRunning` và `m_pendingHoverProbe` trong `AppController`.

- Khi worker đang chạy, request mới không gọi thêm `QMetaObject::invokeMethod`.
- Request pending bị thay thế bởi request mới hơn.
- Probe chính thức được ưu tiên hơn prefetch.
- Khi worker trả kết quả, pending request mới nhất được dispatch.
- Nếu queued invocation thất bại, trạng thái running và metadata được dọn ngay.

File chính:

- `src/app/AppController.h`
- `src/app/AppController.cpp`
- `src/ocr/OcrWorker.h`
- `src/ocr/OcrWorker.cpp`

### Lifecycle stop/reload

Khi dừng OCR, mailbox, token metadata và trạng thái probe đang chạy được reset để request của worker cũ không tiếp tục ảnh hưởng worker mới.

### Region mailbox

`OcrRegionMailbox` được dùng bởi `AppController` để điều phối request OCR vùng.

- Frame mới của region đang chạy chỉ thay thế ảnh/hash/token pending trước đó.
- Chỉ có một request region được dispatch vào worker tại một thời điểm.
- Chỉ request thực sự được dispatch mới gọi `noteRequestQueued()` và tạo Qt queued invocation.
- `OcrWorker::regionFinished` giải phóng trạng thái active và dispatch request mới nhất nếu token/hash còn hợp lệ.
- Region vừa chạy được đưa về cuối hàng đợi round-robin, tránh region cập nhật liên tục chiếm worker.
- Kết quả OCR/plate cũ tiếp tục bị loại bằng token/hash của Phase 0.
- Mailbox được dọn khi stop, reload, đổi ngôn ngữ và xoá region.
- Log `[OCR_METRIC] path=region action=coalesced` cho biết frame trung gian đã bị gộp.

## Quyết định thiết kế còn lại

- Không hủy inference ONNX đang chạy giữa chừng: ONNX Runtime/DirectML không có đường hủy an toàn trong pipeline hiện tại. Request cũ được stale-guard và request mới nhất được chạy sau khi lượt hiện tại hoàn tất.
- Benchmark latency/RAM dài hạn trên game thật tiếp tục thuộc Phase 1/Phase 3; Phase 2 đã kiểm chứng giới hạn queue và fairness ở lớp điều phối.

## Kiểm tra

- `AppController.cpp` compile thành công trong target `EZTranslator`.
- MOC được regenerate thành công.
- Link `EZTranslator.exe` chưa hoàn tất vì executable đang chạy và bị Windows khóa (`Permission denied`).
- Target `ez_ocr`, `hover_probe_test` và `ocr_bench` build thành công sau toàn bộ thay đổi Phase 2.
- `test_ocr_mailbox.exe`: 3 test chính pass, gồm burst 50 request latest-wins, round-robin fairness và lifecycle remove/clear.
- `ctest --test-dir build --output-on-failure`: 100% test pass, 1/1 test.
- Chỉ còn bước link executable chính cần chạy lại sau khi đóng instance `EZTranslator.exe` đang mở.

## Rủi ro và rollback

Rủi ro còn lại là request pending có thể bị bỏ qua khi con trỏ thay đổi quá nhanh; đây là chủ đích latest-wins nhưng cần kiểm tra lại trải nghiệm trên dòng dài và cửa sổ đang cuộn. Có thể rollback policy mailbox bằng cách bỏ dispatch pending, nhưng giữ nguyên request token/stale-result guard của Phase 0.

## Kết luận

Đường hover và region đều đã có giới hạn mailbox ở cấp `AppController`; region có thêm round-robin scheduler và test tự động. Phase 2 đạt tiêu chí triển khai và kiểm thử. Link executable chính bị trì hoãn bởi file đang bị tiến trình hiện tại khóa, không phải lỗi compile/link của mã nguồn.
