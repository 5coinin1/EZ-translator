# Báo cáo triển khai tối ưu OCR và dịch thời gian thực

**Cập nhật:** 27/10/2026  
**Máy mục tiêu:** Windows, i5-11500H, ưu tiên CPU; giữ provider GPU cho máy có GPU phù hợp.  
**Nguồn số liệu:** `build/ez_debug.log`, phiên 26/10/2026 22:14–22:17.  
**Bản build kiểm tra:** `cmake --build build --config Release` — thành công ngày 27/10/2026.  
**Tình trạng:** đã triển khai các thay đổi mã nguồn khả thi cho cả 6 phase và build Release thành công. Các phase đo hiệu năng/độ chính xác ngoài runtime chưa thể được xác nhận vì phiên này không có EZ Translator hoặc game/PDF đang chạy. Không có số liệu sau build nên chưa thể kết luận tốc độ đã tăng.

## 1. Kết quả đọc log trước khi thay đổi

Trong phiên được ghi log, capture dùng WGC ở 2867×1691, cấu hình 30 fps; OCR chạy CPU; dịch dùng CT2 offline Anh → Việt. Một số lượt tiêu biểu:

| Đường xử lý | Thời gian và số lần gọi | Kết luận |
| --- | --- | --- |
| OCR vùng đầy đủ đầu phiên | 2712 ms tổng; detector 409 ms; recognizer 2268 ms; 16 box, 38 `recCalls` | Nhận diện chiếm phần lớn thời gian OCR. |
| OCR vùng đầy đủ nặng | 4987 ms tổng; detector 488 ms; recognizer 4410 ms; 22 box, 61 `recCalls` | Số đoạn nhận diện cao làm thời gian tăng mạnh. |
| OCR dải mới khi cuộn khớp | 897 ms tổng; detector 58 ms; recognizer 816 ms; 12 `recCalls` | Có lợi so với quét toàn vùng, nhưng vẫn tốn recognition. |
| Dịch và hiển thị | Có lượt hoàn chỉnh 1463 ms; các lượt khác 4300–6940 ms | Tổng phản hồi dao động; không thể quy hết cho dịch. |
| Chuyển động/ROI | 20 yêu cầu OCR vùng; 19 full, 1 scroll-band; 13 bị hủy | Có nhiều lượt inference bị bỏ trước khi tạo kết quả dùng được. |

Đây là mẫu trong log, không phải thống kê p50/p95. `recMs` là thời gian theo đồng hồ của bước recognition; các bước có thể chồng lấp nên không cộng máy móc thành tổng pipeline.

## 2. Thay đổi đã tích hợp

### 2.1 Dừng OCR vùng trong chế độ dịch con trỏ

- Khi chuyển sang HoverTranslate, xóa ROI đang chờ, đặt token request vùng về stale, bỏ xác minh cuộn đang chờ và reset scheduler.
- `onCaptureFrame` vẫn cập nhật frame cho hover nhưng thoát trước khi cắt ROI, tính hash vùng và lập lịch OCR ROI.
- Thêm log `[OCR_MODE]` để thấy thời điểm pipeline vùng bị tạm dừng.
- Request ONNX đang chạy có thể phải hoàn tất `Run` hiện tại; kết quả stale không được đưa lên Overlay.

**Tệp:** `src/app/AppController.cpp`.

### 2.2 Giảm OCR bắt đầu giữa một burst cuộn

- Tăng cửa sổ yên trước khi gửi OCR vùng từ 80 ms lên 200 ms. Mục tiêu là đợi frame cuộn cuối cùng ổn định trước khi bắt đầu lượt OCR CPU dài.
- Log OCR vùng bổ sung kích thước ảnh nguồn và thời gian đã tiêu tốn khi request bị hủy. Có thể phân biệt ROI lớn và inference bị bỏ phí trong log tiếp theo.

**Tệp:** `src/app/AppController.cpp`, `src/ocr/OcrWorker.cpp`.

### 2.3 Giữ bản dịch khi cuộn và tái neo theo nội dung

Các thay đổi dưới đây đã có trong working tree trước lượt triển khai này; được giữ nguyên và đã nằm trong bản build mới.

- Tracker Overlay có motion prediction, thử khớp trên dải hẹp chứa chữ khi profile toàn ROI không đủ tín hiệu, và chỉ giữ dịch chuyển khi matcher xác nhận.
- Bản dịch đang hiển thị được giữ trong lúc OCR/dịch phần mới chạy nền. Khi OCR xác nhận nội dung, bản dịch đã cache được neo lại theo đoạn văn.
- Kết quả OCR cũ không được áp lên nội dung mới; trạng thái cuộn tạm thời được dọn khi hết hạn.

**Tệp:** `src/app/AppController.cpp/.h`, `src/overlay/TextOverlay.cpp`, `tests/test_scroll_ocr_reuse.cpp`.

### 2.4 Chế độ GameDialog lọc nền chuyển động

- Thêm tùy chọn `Game dialogue mode` trong phần giao diện/Overlay và lưu thiết lập qua `SettingsStore`.
- Lượt OCR đầu tiên dò vùng thoại theo chu kỳ tối đa 1 giây kể cả khi nền đang chuyển động. Nếu frame đã cũ khi OCR trả về, chỉ thu geometry các box để tạo mask; không dịch và không vẽ chữ từ frame cũ.
- Sau khi có box chữ, scheduler tạo hash từ các vùng chữ đã nhận diện (có đệm nhỏ), thay vì hash toàn ROI. Nền chuyển động ngoài vùng chữ đã biết vì thế không tự kích OCR/dịch. Câu mới hoặc nội dung trong box đổi sẽ đi lại qua debounce 200 ms và OCR thường.
- Khi bật chế độ này, tắt cache box dựa trên hash toàn vùng để tránh lấy geometry cũ cho nền/cảnh mới. Hướng dẫn trong giao diện yêu cầu chọn ROI gọn quanh khung hội thoại.
- Đây là bộ lọc hình ảnh theo box OCR gần nhất, chưa phải nhận dạng ngữ nghĩa hoàn chỉnh: hiệu ứng nền lọt vào box, chữ hiện kiểu typewriter, hoặc đổi cảnh vẫn cần xác minh trên game thật.

**Tệp:** `src/detection/ImageHash.h`, `src/ocr/OcrScheduler.*`, `src/app/AppController.*`, `src/core/Types.h`, `src/core/SettingsStore.cpp`, `src/ui/SettingsDialog.*`.

### 2.5 Cấu hình chi phí recognition

- Giữ giới hạn đoạn recognition theo provider: CPU mặc định 480 px, GPU 1920 px; có thể A/B bằng `EZ_OCR_REC_MAX_WIDTH` trong khoảng 128–1920 px.
- Log đã có `recMs`, `recCalls`, `recCached`, `boxes`, `sourceSize`, provider, số worker và `recMaxWidth`, đủ để so các cấu hình khi chạy cùng ảnh.
- Không đổi giá trị mặc định chỉ dựa trên giả định. Kết quả tối ưu thực tế cần tập ảnh có ground truth để kiểm tra tốc độ song song với CER/WER.

**Tệp:** `src/app/AppController.cpp`, `src/ocr/OcrWorker.cpp`, `src/ocr/OcrRecognizer.cpp`.

## 3. Trạng thái từng phase

| Phase | Phạm vi | Trạng thái thực tế |
| --- | --- | --- |
| 0 — Baseline/đo pipeline | Log có các mốc OCR, dịch và presentation; bổ sung ROI size và chi phí hủy để phân biệt inference bị bỏ. | **Mã đo đã có; baseline runtime còn thiếu.** Chưa có p50/p95, CPU, RAM và FPS sau build theo cùng kịch bản game/PDF. |
| 1 — Tách chế độ và giảm tranh CPU | Không lập ROI OCR trong HoverTranslate; invalidate request vùng cũ khi đổi chế độ. | **Đã triển khai; build thành công.** Chưa kiểm tra tương tác trên game sau build. |
| 2 — Đường nhanh hội thoại game | Có GameDialog mode, hash tập trung trên các box chữ đã biết, dò ban đầu, debounce và bỏ cache geometry toàn ROI. | **Đã triển khai mức nền tảng; runtime chưa xác nhận.** Chưa có bộ kiểm tra typewriter/đổi cảnh/nền động và chưa đo thời gian chữ ổn định đến Overlay. |
| 3 — Cuộn và OCR dải mới | Overlay tracking từng frame, dự đoán chuyển động, dò cột chứa chữ; worker tái sử dụng OCR và nhận dạng band; giữ dịch cũ và tái neo cache. | **Một phần.** Cùng dùng `ScrollTracker`/`matchProfile` nhưng hai phía vẫn có frame tham chiếu riêng; chưa hợp nhất thành một `ScrollObservation`/sequence, chưa đo fallback và sai neo trên video cuộn. |
| 4 — Giảm recognition CPU | Có giới hạn độ rộng đoạn theo CPU/GPU, biến A/B, cache dòng, worker budget và telemetry recCalls/recMs. | **Đường cấu hình/đo đã triển khai; chọn cấu hình tối ưu chưa hoàn tất.** Không có tập ground truth/runtime để xác nhận mức giảm p95 mà không giảm độ chính xác. |
| 5 — Dịch, Overlay, toàn hệ thống | Duy trì CT2 offline, cache/QoS dịch, giữ Overlay lúc xử lý nội dung mới, tái neo đoạn đã dịch và log presentation. | **Tích hợp mã nguồn; xác nhận end-to-end còn thiếu.** Chưa chạy phiên 10 phút hoặc đo p50/p95, FPS, RAM, nhấp nháy và lỗi đè chữ trên cùng workload. |

## 4. Kết quả build và phạm vi kiểm chứng

- `cmake --build build --config Release` — **thành công**, tạo lại `build/EZTranslator.exe`.
- Không chạy unit test/benchmark trong lượt này; build chỉ biên dịch và liên kết các target.
- Không có log runtime được tạo sau khi build mới; do đó chưa có số trước/sau để kết luận tốc độ đã tăng.
- Working tree có các sửa đổi chưa commit từ trước. Không thay thế hoặc gộp chúng vào một commit.

## 5. Cách lấy số đo sau build

1. Chạy bản `build/EZTranslator.exe` mới trên cùng video game và cùng ROI thoại; giữ chế độ Overlay, bật log.
2. Lặp tối thiểu 20 câu thoại mới, gồm nền động và chữ hiện dần. Ghi `OCR_METRIC`, `TRANSLATE_METRIC`, `PIPE_METRIC`, CPU, RAM và FPS game.
3. Lặp cùng thao tác với PDF: cuộn chậm, nhanh, cuộn xa và dừng. Ghi số `scroll-band`, `fallback reason`, `cancelled elapsedMs` và `presentationMs`.
4. So với mốc ở mục 1 bằng p50/p95 theo **cùng kích thước ROI, cùng model/provider, cùng engine dịch và cùng workload**. Nếu đổi nhiều biến cùng lúc, không thể biết thay đổi nào tạo ra kết quả.

## 6. Phần cần chạy runtime để đóng các phase theo tiêu chí nghiệm thu

1. Chạy GameDialog với ROI thoại hẹp: nền động nhưng câu đứng yên, chữ hiện dần, câu mới cùng vị trí, thoại biến mất/đổi cảnh. Kiểm tra số OCR/dịch, thời gian hiển thị và việc giữ/xóa overlay.
2. Chạy PDF/web với cuộn chậm, nhanh, hai chiều, cuộn xa, zoom và nhảy trang. Hiện tại Overlay tracker và worker `ScrollOcrReuse` vẫn so với hai mốc ảnh riêng; cần đo trước khi thiết kế/hợp nhất observation để không làm tăng ghép nhầm.
3. A/B `EZ_OCR_REC_MAX_WIDTH` (ít nhất 320/480/640/960), worker/thread và model trên cùng bộ ảnh có ground truth; ghi CER/WER cùng p50/p95, CPU, RAM và FPS.
4. Chạy ít nhất 20 câu thoại và phiên tài liệu 10 phút sau build; so với log baseline ở mục 1 bằng cùng provider, engine, ROI và workload.

Các phase đã được triển khai đến mức mã nguồn có thể hoàn tất trong môi trường hiện tại. Tiêu chí chất lượng và hiệu năng runtime vẫn để **chưa xác nhận**; build thành công không chứng minh app đã nhanh hơn hoặc không còn lỗi hình học. Để điền số liệu cần chạy bản build mới trên workload thật và đọc log sau lượt đó.
