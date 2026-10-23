# Kế hoạch tối ưu độ trễ OCR trên CPU và GPU

## 1. Mục tiêu

Giảm chênh lệch thời gian phản hồi giữa máy có GPU rời và máy chỉ có CPU/iGPU,
đồng thời giữ độ chính xác OCR, không làm RAM tăng dần và không để yêu cầu cũ
chặn kết quả mới.

Phạm vi đo độ trễ đầu cuối:

```text
capture -> schedule -> detect -> recognize -> translate -> render popup/overlay
```

Mục tiêu không phải làm CPU nhanh ngang mọi GPU, mà bảo đảm máy CPU vẫn phản hồi
ổn định, không backlog và không có tình trạng chờ vô hạn.

## 2. Hiện trạng trong mã nguồn

### 2.1 Các tối ưu đã có

- Capture GDI và WGC có cơ chế `framePending`, tránh xếp vô hạn ảnh toàn màn hình.
- OCR chạy trên worker thread, không khóa trực tiếp UI thread.
- `OcrScheduler` có settle, cooldown và giới hạn số lượt OCR mỗi tick.
- Recognizer có cache theo nội dung dòng.
- Recognizer GPU có dynamic batch và có thể dùng model FP16.
- Recognizer CPU có thể chạy nhiều dòng song song.
- Overlay và translation có cache theo nội dung.
- App đã tự chuyển OCR sang CPU nếu không phát hiện GPU rời đủ VRAM.

### 2.2 Các điểm nghẽn chính

| Ưu tiên | Điểm nghẽn | Vị trí chính |
| --- | --- | --- |
| P0 | Hover gửi probe nhanh hơn tốc độ OCR CPU | `AppController::onHoverTick`, `sendHoverProbe` |
| P0 | Probe mới thay token, kết quả CPU cũ liên tục bị bỏ | `AppController::onHoverProbed` |
| P1 | Ảnh lớn bị chia 2x2 và chạy cả detect lẫn recognize trên từng tile | `OcrEngine::recognize` |
| P1 | Detector, recognizer và dịch offline có thể tranh toàn bộ CPU | `OcrRuntime`, `OcrRecognizer`, các translation engine |
| P1 | CPU và GPU dùng gần như cùng một cấu hình đầu vào | `OcrOptions`, `AppController::ocrOptions` |
| P1 | Google GTX trả HTTP 429, mỗi dòng có thể chờ timeout rồi mới fallback | `GoogleTranslator`, `TranslationWorker` |
| P2 | Worker vùng và hover nạp session/model OCR riêng | `AppController::spawnOcrWorker` |
| P2 | WGC helper chưa được deploy đúng cho một số kiểu build, phải fallback GDI | `WgcWindowCapture`, `CMakeLists.txt` |

### 2.3 Vấn đề hover cần sửa trước

Hover hiện có thể gửi yêu cầu mới mỗi 100-120 ms. `m_hoverProbes` chỉ giữ tối đa
8 phần tử, nhưng xóa phần tử khỏi map không xóa các `QMetaCallEvent` đã nằm trong
event queue của worker.

Khi OCR CPU trả kết quả chậm:

1. Token mới đã thay `m_hoverLastFinalToken`.
2. Kết quả cũ bị bỏ vì token không còn là token cuối.
3. Yêu cầu tiếp tục được gửi trong khi worker vẫn xử lý backlog.
4. Người dùng không thấy popup dù OCR vẫn đang chạy.

Đây là lỗi điều phối yêu cầu, không chỉ là vấn đề tốc độ model.

## 3. Chỉ số và ngưỡng nghiệm thu

### 3.1 Chỉ số bắt buộc

Mỗi yêu cầu cần có một `requestId` và các mốc thời gian:

- `captureReadyAt`
- `scheduledAt`
- `queueEnteredAt`
- `ocrStartedAt`
- `detFinishedAt`
- `recFinishedAt`
- `translateStartedAt`
- `translateFinishedAt`
- `presentedAt`

Log cần bổ sung:

- Provider: CPU, DirectML hoặc CUDA.
- Kích thước ảnh và số pixel.
- Độ sâu hàng đợi và số yêu cầu bị gộp hoặc loại bỏ.
- Số box, số lần gọi recognizer và cache hit.
- Thời gian preprocess, model run và decode.
- Engine dịch, cache hit, HTTP status và fallback.
- Working set/RAM theo chu kỳ.

### 3.2 Mục tiêu hiệu năng

| Tình huống | Mục tiêu |
| --- | --- |
| Hover OCR CPU | p50 <= 250 ms, p95 <= 450 ms |
| Hover OCR GPU rời | p95 <= 200 ms |
| OCR vùng 1080p trên CPU | p95 <= 1.000 ms |
| Hàng đợi hover | Tối đa 1 running + 1 pending |
| Kết quả bị token mới làm mất | Không xảy ra starvation |
| RAM sau 10 phút | Ổn định, không tăng theo số frame |
| Độ chính xác | Không giảm quá 1% so với baseline |

Độ trễ dịch online phải được báo cáo riêng, không tính chung vào benchmark OCR.

## 4. Lộ trình triển khai

### Giai đoạn 1: Instrumentation và benchmark

#### Công việc

- Tạo cấu trúc `PipelineTrace` hoặc `OcrRequestMetrics` dùng chung.
- Đưa thời gian chờ trong queue vào log, thay vì chỉ log `detMs` và `recMs`.
- Nâng cấp `tools/ocr_bench.cpp` để xuất JSON hoặc CSV.
- Bổ sung benchmark hover latest-wins với fake OCR session có độ trễ cố định.
- Tạo bộ dữ liệu benchmark cố định:
  - Hover band ở 720p và 1080p.
  - ROI nhỏ, vừa và toàn màn hình.
  - Ảnh tĩnh và ảnh đang cuộn.
  - Tiếng Anh, Trung và Nhật.
  - Chữ nhỏ, chữ lớn, nền sáng và nền tối.
- Chạy trên ít nhất ba nhóm máy:
  - CPU 4 core, không có GPU rời.
  - CPU 8 core, không có GPU rời.
  - GPU rời tầm trung.

#### File dự kiến

- `src/ocr/OcrTypes.h`
- `src/ocr/OcrWorker.*`
- `src/app/AppController.*`
- `tools/ocr_bench.cpp`
- `tools/hover_probe_test.cpp`

#### Điều kiện hoàn thành

- Có báo cáo p50 và p95 cho từng stage.
- Phân biệt được model chậm, queue chậm hay mạng chậm.
- Benchmark có thể lặp lại trên máy khác với cùng dữ liệu đầu vào.

### Giai đoạn 2: Backpressure và latest-wins

#### Công việc

- Thay queue hover hiện tại bằng mailbox:
  - Tối đa một yêu cầu đang chạy.
  - Tối đa một yêu cầu mới nhất đang chờ.
  - Yêu cầu mới thay thế yêu cầu pending cũ.
- Thêm signal hoặc callback `requestFinished` để gửi yêu cầu pending mới nhất.
- Ưu tiên probe chính thức hơn prefetch.
- Trên CPU, chỉ prefetch khi worker hoàn toàn rảnh.
- Gộp OCR vùng theo `regionId + imageHash`.
- Kiểm tra yêu cầu stale trước preprocess và trước model run nếu có thể.
- Không dùng kích thước `m_hoverProbes` làm queue depth vì map token không phản ánh
  event queue của Qt.

#### File dự kiến

- `src/app/AppController.h`
- `src/app/AppController.cpp`
- `src/ocr/OcrWorker.h`
- `src/ocr/OcrWorker.cpp`

#### Test bắt buộc

- Fake OCR mất 500 ms, gửi 50 probe trong 2 giây.
- Chỉ yêu cầu đầu và yêu cầu mới nhất được xử lý.
- Queue không vượt quá hai yêu cầu.
- Popup hiển thị kết quả của vị trí chuột cuối cùng.
- RAM không tăng theo số probe.

### Giai đoạn 3: Profile CPU/GPU và ngân sách thread

#### Thiết kế

Thêm `OcrExecutionProfile`:

```text
Auto
CpuBalanced
DiscreteGpu
```

`Auto` là mặc định. Người dùng có thể ghi đè thành CPU hoặc GPU trong Settings.

#### CPU profile

- Detector dùng 2-4 intra-op thread tùy số core vật lý.
- Recognizer dùng ngân sách CPU chung, không tự mở tới 6 worker trong mỗi engine.
- OCR và dịch offline không được đồng thời chiếm toàn bộ core.
- Ưu tiên hover, sau đó mới đến OCR vùng và dịch nền.
- Dùng một `Ort::Env` chia sẻ để tránh tạo nhiều thread pool ONNX riêng.

#### GPU profile

- Bật FP16 khi model FP16 tồn tại và độ chính xác đạt ngưỡng.
- Dynamic batch theo nhóm chiều rộng gần nhau.
- Giới hạn batch theo VRAM và độ trễ, không chỉ theo throughput.
- Không chạy song song nhiều `Run` trên DirectML.

#### Lựa chọn provider tự động

- Phát hiện GPU rời như hiện tại.
- Warm-up bằng các shape thực tế của hover và ROI.
- Nếu GPU rời vẫn chậm hơn CPU hoặc khởi tạo lỗi, fallback CPU cho phiên hiện tại.
- Log rõ lý do và provider thực tế.

#### File dự kiến

- `src/ocr/OcrTypes.h`
- `src/ocr/OcrRuntime.*`
- `src/ocr/OcrRecognizer.cpp`
- `src/app/AppController.cpp`
- `src/core/Types.h`
- `src/ui/SettingsDialog.cpp`

### Giai đoạn 4: Tối ưu hover trên CPU

#### Công việc

- Giảm band ban đầu từ `1200x96` xuống khoảng `768-896x96` trên CPU.
- Detector CPU chạy lần đầu với `detLimitSide=512`.
- Retry với 640 nếu không có box, confidence thấp hoặc box chạm mép.
- Sửa `clippedByBand`: hiện worker luôn emit `false`, nên nhánh mở rộng band không chạy.
- Recognition vẫn crop từ ảnh gốc, không crop từ ảnh detector đã resize.
- Chỉ OCR lại khi con trỏ sang dòng khác hoặc nội dung frame thay đổi thật.
- Cache kết quả theo `imageHash + lineRect`, không chỉ theo text.

#### Điều kiện hoàn thành

- CPU hover không tạo backlog.
- Di chuyển chuột nhanh không làm OCR tất cả vị trí trung gian.
- Dòng dài vẫn được đọc đầy đủ sau khi band mở rộng.
- Tỷ lệ nhận sai không tăng quá ngưỡng benchmark.

### Giai đoạn 5: Refactor OCR ảnh lớn

Hiện tại mỗi tile gọi `recognizeProcessed()`, nên mỗi tile đều chạy cả detector và
recognizer. Luồng mới:

```text
detect theo tile
    -> quy đổi box về tọa độ ảnh gốc
    -> loại box trùng
    -> recognize toàn bộ box một lần
```

#### Công việc

- Tách API `detectTiles()` và `recognizeBoxes()`.
- Loại box trùng trước recognition.
- Không tile ROI nhỏ.
- Chạy detector coarse một lần; chỉ tile khi chữ nhỏ hoặc coarse detect bỏ sót.
- Thử các cấu hình 1 pass, 2 tile và 4 tile bằng benchmark.
- Không dùng ngưỡng kích thước ảnh cố định làm điều kiện duy nhất.

#### File dự kiến

- `src/ocr/OcrEngine.cpp`
- `src/ocr/OcrDetector.*`
- `src/ocr/OcrRecognizer.*`
- `tests/test_detection.cpp`

### Giai đoạn 6: Tối ưu capture và bộ nhớ

#### Công việc

- Bảo đảm `wgc_capture_helper.exe` nằm cạnh executable cho mọi build config.
- Thêm kiểm tra startup xác nhận WGC helper tồn tại và handshake thành công.
- Chỉ dùng GDI làm fallback và log rõ lý do fallback.
- Giảm copy `QImage` không cần thiết; crop ROI càng sớm càng tốt.
- Dùng FPS thích ứng:
  - Tăng khi đang cuộn hoặc có chuyển động.
  - Giảm khi frame tĩnh.
- Giữ cơ chế một frame pending hiện tại.
- Giới hạn cache theo byte cho line cache, plate cache và frame cache.

#### File dự kiến

- `src/capture/WgcWindowCapture.cpp`
- `src/capture/GdiWindowCapture.cpp`
- `src/app/AppController.cpp`
- `CMakeLists.txt`

### Giai đoạn 7: Tối ưu translation và rate limit

#### Công việc

- Khi Google GTX trả HTTP 429, mở circuit breaker trong 30-60 giây.
- Trong thời gian breaker, fallback ngay thay vì thử Google cho từng dòng.
- Gộp nhiều unit trong một request nếu API hỗ trợ.
- Hủy yêu cầu hover cũ khi con trỏ đã sang dòng mới.
- Ưu tiên cache và offline engine nếu đã sẵn sàng.
- Tách thread budget của dịch offline khỏi OCR CPU.
- Hiển thị trạng thái rõ: `OCR xong, engine dịch đang bị giới hạn`.

#### File dự kiến

- `src/translation/GoogleTranslator.cpp`
- `src/translation/MyMemoryTranslator.cpp`
- `src/translation/TranslationWorker.*`
- `src/app/AppController.cpp`

### Giai đoạn 8: Đánh giá model CPU

Chỉ thực hiện sau khi queue và pipeline đã được sửa, vì đổi model không khắc phục
được backlog.

#### Thử nghiệm

- PP-OCR mobile detector/recognizer hiện tại.
- Model ONNX INT8 hoặc QDQ tối ưu cho CPU.
- FP32 với các cấu hình thread khác nhau.
- So sánh model tiếng Anh riêng và model đa ngôn ngữ.

#### Điều kiện chấp nhận model mới

- p95 nhanh hơn ít nhất 20% trên máy CPU mục tiêu.
- Text recall và CER không giảm quá 1%.
- Không tăng số box rác trên UI/game.
- RAM và kích thước package nằm trong ngân sách.

## 5. Thứ tự pull request đề xuất

1. `perf/metrics-benchmark`: instrumentation và benchmark có thể lặp lại.
2. `perf/ocr-latest-wins`: backpressure, coalesce và test queue.
3. `perf/cpu-execution-profile`: CPU thread budget và shared ORT environment.
4. `perf/hover-adaptive-band`: band/limit thích ứng và sửa `clippedByBand`.
5. `perf/tiled-detect-once`: detect tile, loại box trùng, recognize một lần.
6. `perf/wgc-packaging`: deploy helper và benchmark capture.
7. `perf/translation-circuit-breaker`: xử lý HTTP 429 và fallback nhanh.
8. `perf/cpu-model-evaluation`: đánh giá INT8/mobile model.

Mỗi pull request phải kèm benchmark trước/sau và test regression. Không gom tất cả
thay đổi vào một pull request lớn vì sẽ khó xác định tối ưu nào thực sự có tác dụng.

## 6. Ưu tiên thực tế

Thứ tự nên làm ngay:

1. Instrumentation tối thiểu.
2. Latest-wins và giới hạn queue hover.
3. CPU thread budget.
4. Hover band và detector resolution thích ứng.
5. Refactor tile để chỉ recognize một lần.
6. Circuit breaker cho Google 429.
7. WGC packaging và adaptive FPS.
8. Thử model INT8 sau cùng.

Ba mục đầu có khả năng cải thiện trải nghiệm CPU lớn nhất mà không đổi model và có
rủi ro ảnh hưởng độ chính xác thấp nhất.
