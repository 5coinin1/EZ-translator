# ocr/

Đọc chữ trên ảnh ROI. **Tách Detection và Recognition thành 2 giai đoạn riêng biệt.**

## Giai đoạn

### 1. Detection — `OcrDetector`
Model: `ch_PP-OCRv4_det_infer.onnx` (DB, **luôn fp32**).

- Ảnh ROI resize sao cho cạnh dài ≤ `detLimitSide` (mặc định 640, bội số 32).
- Output heatmap xác suất "là chữ"; threshold `detThreshold` (0.3).
- Nhị phân hoá → connected components → mỗi blob là một hộp.
- `unclipRatio` (1.6) nở hộp ra (text thật to hơn vùng mực) → trả `OcrTextBox{rect, text=""}`.

Đây là nơi quyết định box nằm ở đâu. Muốn thêm/sửa box → sửa ở đây (hoặc tầng assembler).

### 2. Recognition — `OcrRecognizer`
Model: `en_PP-OCRv4_rec_mobile.onnx` hoặc `ch_PP-OCRv4_rec_infer.onnx`.

- Crop ảnh theo từng box, resize cao 48 giữ tỉ lệ; dòng dài chia đoạn ≤ `recMaxWidth`.
- **Batch theo bucket bề rộng** (~48 dòng/lô trên GPU) → 1 Run cho nhiều dòng.
- Output logits CTC → greedy decode bằng từ điển (`en_dict.txt` / `ppocr_keys_v1.txt`),
  bỏ blank và ký tự lặp.
- **Cache theo nội dung dòng**: dòng không đổi được phục vụ từ cache (0 ms).
- fp16 chỉ áp cho rec (nếu có `*_fp16.onnx` cạnh model); det luôn fp32.

Recognition chỉ đọc nội dung của box do detection cung cấp.

### 3. Ghép văn bản — `OcrTextAssembler`
Nhóm box theo hàng (tâm y gần nhau) → sắp trái→phải → nối text (chèn khoảng trắng
theo khe) → phát hiện đoạn theo khe dòng / thụt đầu dòng → `assemble` (text có
`\n`/`\n\n`) và `assembleLines`/`assembleDocument` (giữ rect từng dòng).

### 4. Điều phối — `OcrEngine`
`recognize(image)` = `OcrDetector.detect` → `OcrRecognizer.recognize`. Giữ ONNX
Runtime kín trong `OcrRuntime` (Detector/Recognizer không phụ thuộc header ORT).

## Execution Provider (GPU → CPU fallback)

`OcrRuntime` chọn provider lúc chạy: thử **DirectML/CUDA**, nếu build ORT không có
symbol hoặc append/session lỗi thì **tự fallback về CPU** — không cần cấu hình gì.
Máy không có GPU rời (hoặc không có DX12) vẫn chạy được.

- `OcrRuntime::configure(useGpu)` trả tên provider thực tế; `OcrEngine::providerName()`
  cho biết đang chạy `DirectML`/`CUDA`/`CPU`.
- Trên **GPU**: rec **batch** nhiều dòng (DirectML không cho Run song song).
- Trên **CPU**: tắt batch, chạy các dòng **song song** nhiều worker (tối ưu cho máy
  không GPU).
- App hiển thị provider lên thanh trạng thái; cờ `AppSettings::ocrUseGpu` (mặc định
  true) để buộc CPU nếu cần.

## Tài nguyên

- ONNX Runtime (DirectML) vendored ở `third_party/onnxruntime/`.
- Model + từ điển ở `models/`.
- Override thư mục model lúc chạy bằng biến môi trường `EZ_MODELS_DIR`.

## Kiểm tra nhanh

```
build\Release\ocr_smoke.exe
```
Render ảnh có chữ rồi chạy det+rec, in box/text/timing.

## Chi phí tham khảo (DirectML)

| Bước | Thời gian |
|---|---|
| det | ~12 ms |
| rec | ~170 ms cho 36 dòng (lần đầu chậm hơn do warmup) |
| rec (cache) | ~0 ms cho dòng đã gặp |
