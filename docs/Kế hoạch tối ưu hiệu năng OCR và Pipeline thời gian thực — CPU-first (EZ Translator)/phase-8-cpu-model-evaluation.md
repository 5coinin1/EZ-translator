# Báo cáo Phase 8 — CPU Model Evaluation

## Trạng thái

**Đạt một phần**

Đã triển khai harness đánh giá reproducible; chưa thể chốt model mới vì repository hiện chỉ có model đang dùng và chưa có dataset ground-truth.

## Đã làm

- Thêm `build/ocr_model_eval.exe`.
- Harness đọc ảnh `png/jpg/jpeg` trong `EZ_OCR_EVAL_DATASET` và file kỳ vọng cùng tên `.txt`.
- Hỗ trợ chọn recognition model/dictionary qua `EZ_OCR_EVAL_REC_MODEL` và `EZ_OCR_EVAL_DICT`.
- Xuất CSV theo ảnh gồm kích thước expected/actual, CER, exact match, số box và `totalMs`.
- In provider thật, số sample, mean CER và exact count ra stderr.
- Áp dụng cùng CPU/GPU thread budget của pipeline để kết quả không bị lệch cấu hình.

## Cách chạy khi có dataset

```text
EZ_OCR_USE_GPU=0
EZ_OCR_EVAL_DATASET=D:/dataset/ocr
EZ_OCR_EVAL_REC_MODEL=D:/models/candidate_rec.onnx
build/ocr_model_eval.exe > candidate.csv
```

Mỗi ảnh cần có sidecar text cùng basename, ví dụ `scene-01.png` và `scene-01.txt`. So sánh candidate với model hiện tại trên cùng ảnh, cùng preprocessing và cùng số lượt warm/cold.

## Kiểm tra hiện tại

- `ocr_model_eval` build thành công.
- CTest pass `3/3`.
- Chưa chạy evaluation vì không có dataset và candidate model trong repo; không bịa CER/recall hay tuyên bố INT8 tốt hơn.

## Kết luận

Phần chạy thử đã sẵn sàng. Acceptance cuối của Phase 8 vẫn cần dữ liệu thực tế, model candidate hợp lệ và kiểm tra license/package trước khi thay model mặc định.
