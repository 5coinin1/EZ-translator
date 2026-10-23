# Báo cáo Phase 3 — CPU Thread Budget

## Trạng thái

**Đạt**

Đã triển khai ngân sách thread CPU/GPU cho detector, recognizer, OCR worker hover/vùng và engine dịch offline. Các profile mặc định, override và CTest đều đã được kiểm tra.

## Phạm vi

Ngày triển khai: 2026-10-23

Mục tiêu:

1. Không để detector/recognizer tự mở quá nhiều thread trên CPU.
2. Giữ GPU path tuần tự, tránh nhiều `Run` tranh chấp trên DirectML/CUDA.
3. Giới hạn recognizer CPU theo budget thay vì cố định tới 6 worker.
4. Dành budget riêng cho hover worker để không tranh hết CPU với region worker.
5. Giảm thread của sidecar CTranslate2 và Marian khi chạy CPU.

## Thay đổi đã thực hiện

### OCR budget

Thêm `OcrThreadBudget` tại `src/ocr/OcrThreadBudget.*`.

- CPU region mặc định: detector tối đa 2 intra-op, recognizer 1 intra-op, recognizer worker 2–4 tùy logical CPU, inter-op 1.
- CPU hover mặc định: detector 1, recognizer worker 1 vì hover thường chỉ nhận một dòng.
- GPU mặc định: detector 1, recognizer 1, recognizer worker 1, inter-op 1.
- `OcrRuntime` nhận riêng intra-op/inter-op cho từng session.
- Recognizer sử dụng `OcrOptions::recWorkers` thay vì tự mở tới 6 worker.
- Log lúc load ghi provider thật và budget thực tế:
  `[OCR] thread budget provider=... detIntra=... recIntra=... recWorkers=... interOp=...`

### Translation budget

Thêm `TranslationThreadBudget` tại `src/translation/TranslationThreadBudget.*`.

- CTranslate2 sidecar mặc định dùng `max(1, logicalThreads / 4)` thay vì `/2`.
- Marian ONNX dùng cùng budget translation khi được nạp.
- Log CT2 ghi số thread truyền sang sidecar.

### A/B override

Các biến môi trường phục vụ đo profile mà không sửa code:

- `EZ_OCR_DET_THREADS`
- `EZ_OCR_REC_THREADS`
- `EZ_OCR_REC_WORKERS`
- `EZ_OCR_INTER_THREADS`
- `EZ_TRANSLATE_THREADS`

Giá trị được clamp trong giới hạn an toàn; giá trị không hợp lệ bị bỏ qua.

## Benchmark đã chạy

Công cụ: `build/ocr_bench.exe --csv`, provider CPU, model hiện tại, workload synthetic cùng input. Đã bổ sung `pipeline-roi` gồm 6 dòng để kích hoạt recognizer worker song song.

Profile mặc định của môi trường chạy hiện tại: `det=1, rec=1, workers=2, inter=1`.

| Profile | Hover median | ROI median | Ghi nhận |
| --- | ---: | ---: | --- |
| `det=1, workers=2` | khoảng 221 ms | khoảng 1.247 ms | profile mặc định |
| `det=2, workers=2` | khoảng 237 ms | khoảng 1.211 ms | ROI tương đương, hover không tốt hơn |
| `det=2, workers=4` | khoảng 218 ms | khoảng 864 ms | ROI synthetic nhanh hơn, cần kiểm tra contention trên game thật |

Các lượt đo trên cho thấy thread budget đã được áp dụng thật. Không kết luận profile `workers=4` là mặc định tốt cho mọi máy vì workload synthetic có 6 box và chưa đại diện cho game đang chạy cùng dịch.

## Kiểm tra

- `test_ocr_thread_budget.exe`: 3 test chính pass, gồm profile mặc định CPU/GPU, override/clamp và translation budget.
- `test_ocr_mailbox.exe`: tiếp tục pass các test bounded scheduling của Phase 2.
- `ctest --test-dir build --output-on-failure`: 100% pass, 2/2 test.
- `ez_translate`, `ez_ocr`, `ocr_bench`, `hover_probe_test` build thành công.
- `AppController.cpp` compile thành công; link `EZTranslator.exe` vẫn bị Windows khóa vì instance app đang chạy.

## Quyết định và giới hạn

- Không dùng global ORT thread pool như một giả định tối ưu; mỗi session nhận budget rõ ràng và phải đo tiếp.
- Không đồng thời tăng intra-op và inter-op; inter-op giữ 1 để tránh nhân thread ngoài ý muốn.
- Không ép CPU phải nhanh bằng GPU; mục tiêu là p95/RAM ổn định và UI/game không bị tranh CPU.
- Benchmark target i5-12500H cùng game thật, CPU utilization, working set dài hạn, accuracy và offline translation end-to-end cần được chạy trong vòng validation tiếp theo.

## Kết luận

Phase 3 đã hoàn tất phần triển khai thread budget và kiểm thử tự động. Profile mặc định CPU-first đã được áp dụng cho pipeline OCR và offline translation; các profile khác có thể A/B bằng biến môi trường mà không cần sửa binary.
