# Báo cáo Phase 1 — Instrumentation & CPU Baseline

## Trạng thái

**Đạt một phần**

Instrumentation và công cụ xuất dữ liệu đã được triển khai. Benchmark thực tế trên i5-12500H, game đang chạy, CPU-only/DirectML và các workload accuracy chưa được chạy trong phase này.

## Phạm vi

Ngày triển khai: 2026-10-23

Mục tiêu:

- Đo queue wait trước khi OCR bắt đầu.
- Đo tổng OCR, detection, recognition, tile và alternate model.
- Tách số lần gọi model khỏi số dòng lấy từ cache.
- Ghi latency translation và presentation ở pipeline thực.
- Cho `ocr_bench` xuất CSV có schema ổn định.

## Thay đổi đã thực hiện

### 1. OCR timing theo từng request

Mở rộng `OcrTimings` với:

- `totalMs`
- `detCalls`
- `tileCount`
- `altCalls`
- detector/recognizer/cache counters theo request

`OcrEngine` hiện cộng timing của nhiều tile và alternate recognizer thay vì chỉ trả timing cuối cùng. `OcrRecognizer` reset counter khi bắt đầu request để số liệu không cộng dồn vô hạn qua nhiều frame.

File:

- `src/ocr/OcrTypes.h`
- `src/ocr/OcrEngine.cpp`
- `src/ocr/OcrRecognizer.cpp`

### 2. Queue wait và runtime OCR log

Thêm timestamp monotonic dùng chung giữa UI và worker qua `ocrMonotonicMs()`.

Region và hover truyền `queuedAtMs` sang worker. Worker ghi log dạng:

`[OCR_METRIC] path=region|hover request=... queueMs=... totalMs=... detMs=... recMs=... detCalls=... tiles=... altCalls=... boxes=... recCalls=... recCached=...`

File:

- `src/ocr/OcrWorker.h`
- `src/ocr/OcrWorker.cpp`
- `src/app/AppController.cpp`

### 3. Translation/presentation metrics

Thêm log `[PIPE_METRIC]` cho:

- Hover translation latency và trạng thái presentation.
- Region translation latency.
- Tổng thời gian từ dispatch OCR đến pipeline presentation.
- Số translation unit thực sự chạy trong request.

File:

- `src/app/AppController.cpp`

### 4. CSV benchmark

`ocr_bench --csv` hiện xuất các workload:

- `det-same`
- `det-same-size`
- `rec`
- `pipeline-hover`

Cột gồm provider, total/det/rec timing, số detector call, tile, alternate call, box, recognition call và cache hit. Chế độ mặc định cũ vẫn giữ log human-readable.

File:

- `tools/ocr_bench.cpp`

## Kiểm tra

- `cmake --build build --target ez_ocr hover_probe_test -j 4`: đạt.
- `cmake --build build --target ocr_bench -j 4`: đạt.
- `EZTranslator` compile đến `AppController.cpp`: đạt.
- Link `EZTranslator.exe`: chưa hoàn tất vì executable đang chạy và bị Windows khóa (`Permission denied`).
- Chưa chạy `ocr_bench` để tránh tự tạo số liệu baseline chưa được kiểm soát.
- Chưa có dataset ground truth CER/recall/reading order.

## Phần chưa hoàn tất

- Chạy baseline cold/warm trên i5-12500H CPU-only.
- Chạy workload hover ngắn/dài, ROI, menu, CJK, scroll và full-window.
- Đo khi game chạy và khi có offline translation đồng thời.
- Đo timeout/429 online.
- Ghi CPU utilization, working set/peak RAM và cache hit rate theo phiên chạy.
- Kiểm tra accuracy với ground truth.

## Cách chạy benchmark sau khi đóng executable đang khóa

```text
build\ocr_bench.exe --csv > ocr-baseline.csv 2> ocr-baseline.log
```

CPU-only:

```text
set EZ_OCR_USE_GPU=0
build\ocr_bench.exe --csv > ocr-baseline-cpu.csv 2> ocr-baseline-cpu.log
```

Các lệnh trên là hướng dẫn tái hiện, chưa phải kết quả benchmark đã chạy.

## Rủi ro và rollback

Instrumentation có thể làm log nhiều hơn và ảnh hưởng nhẹ đến timing. Có thể giảm tần suất log hoặc tắt log metric ở phase sau nếu benchmark đo được overhead đáng kể; không cần rollback các thay đổi correctness của Phase 0.

## Kết luận

Phase 1 đã tạo được nền đo có thể phân biệt queue wait, model time, cache hit, tiled/alternate work và translation/presentation latency. Chưa được coi là baseline hoàn chỉnh cho đến khi chạy workload thực tế và lưu raw CSV cùng thông tin hardware/model/build.
