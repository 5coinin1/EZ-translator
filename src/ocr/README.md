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
- fp16 chỉ áp cho rec trên GPU (nếu có `*_fp16.onnx` cạnh model); det luôn fp32.
- CPU: tự dùng `*_int8conv.onnx` nếu có (hiện là `en_PP-OCRv4_rec_mobile_int8conv.onnx`).
  Model này chỉ quantize Conv (QDQ int8), giữ classifier/Softmax fp32: nhanh
  khoảng 2.5-3x, file ~2.9 MB thay vì 7.6 MB. Tắt bằng `EZ_OCR_REC_INT8=0`.

Recognition chỉ đọc nội dung của box do detection cung cấp.

### 3. Ghép văn bản — `OcrTextAssembler`
Nhóm box theo hàng (tâm y gần nhau) → sắp trái→phải → nối text (chèn khoảng trắng
theo khe) → phát hiện đoạn theo khe dòng / thụt đầu dòng → `assemble` (text có
`\n`/`\n\n`) và `assembleLines`/`assembleDocument` (giữ rect từng dòng).

### 4. Điều phối — `OcrEngine`
`recognize(image)` = `OcrDetector.detect` → `OcrRecognizer.recognize`. Giữ ONNX
Runtime kín trong `OcrRuntime` (Detector/Recognizer không phụ thuộc header ORT).

### 5. Lên lịch — `OcrScheduler`
Quyết định **khi nào** chạy OCR cho một vùng, thay cho "đổi là chạy ngay":

- **settle** (`settleMs`): vùng phải đứng yên đủ lâu mới chạy — tránh OCR giữa
  lúc nội dung/hoạt ảnh còn đang đổi. Giá trị theo profile: Low 40ms,
  Balanced 20ms, High 15ms.
- **cooldown** (`minRunIntervalMs`): hai lần chạy cùng vùng cách nhau tối thiểu.
  Low 180ms, Balanced 90ms, High 60ms.
- **cap** (`maxRunsPerTick`, 1): tối đa vài lần chạy mỗi tick khi có nhiều vùng.
- Hash nội dung dùng `imageDifferenceHash` (dHash, chung với `detection/`).

`AppController` gọi `beginTick()` rồi `process()` cho từng vùng mỗi frame; chỉ vùng
`run=true` mới OCR. Vùng `deferred` **giữ box cũ** → overlay không nhấp nháy.

## Luồng CPU / GPU

```
AppController::ocrOptions(hoverWorker)
  ├─ probeDiscreteGpuForOcr()  // DXGI: GPU rời/Intel Arc, VRAM >= 512MB, lấy adapter index
  ├─ detectOcrResourcePlan()   // cores + RAM + VRAM -> Low / Balanced / High
  ├─ options.useGpu = ocrUseGpu && gpu.available
  ├─ options.gpuAdapterIndex = gpu.adapterIndex
  └─ chooseOcrThreadBudget(useGpu, hoverWorker, profile)
       ├─ GPU: det=1, rec=1, recWorkers=1, inter=1; recMaxWidth theo VRAM
       └─ CPU: Low det=1/workers=2; Balanced-High det=1-2/workers tối đa 4; recMaxWidth=480; rec int8 Conv
```

`OcrRuntime` chọn provider lúc chạy: thử **CUDA/DirectML** với đúng
`gpuAdapterIndex`, nếu build ORT không có symbol hoặc append/session lỗi thì
**tự fallback CPU**.

`OcrWorker::load` kiểm tra provider **thực tế** sau khi nạp:

- Nếu xin GPU nhưng provider = CPU, hoặc nạp GPU lỗi → nạp lại bằng option CPU
  (không fp16, `recMaxWidth=480`, budget CPU, cache theo profile).
- Log `[OCR] CPU fallback reason=...`; status bar ghi rõ fallback.
- Nếu máy không có GPU rời / người dùng tắt `ocrUseGpu` → đi thẳng CPU.

Provider thực tế: `OcrEngine::providerName()` = `DirectML` / `CUDA` / `CPU`.

### Progressive OCR trên CPU

Full OCR trên CPU là đường chậm nhất khi trang mới: phải nhận dạng 18-25 dòng,
cache nội dung không thể trúng. `OcrEngine::recognize` trên CPU chia box thành
cụm (mặc định bằng số rec worker, tối đa 6) và gọi callback sau mỗi cụm.
`OcrWorker` phát `regionRecognizedPartial` cho cụm đầu; `AppController` dùng
preview để dịch/hiện sớm, nhưng **không cache box, không ghim line pitch và
không dựng plate**; kết quả full ngay sau đó thay thế bằng box đầy đủ.

- Tổng thời gian full gần như không đổi (+~20-50 ms do chia cụm).
- Phản hồi đầu tiên trên máy 16 core: ~250 ms thay vì chờ 0.8-3 s.
- GPU không dùng progressive vì batch cả trang đã nhanh.
- Tắt/A-B: `EZ_OCR_PROGRESSIVE=0`; đổi cụm: `EZ_OCR_PROGRESSIVE_CHUNK=<2..12>`.

> Phần **dịch offline** dùng CTranslate2: CPU int8 hoặc CUDA float16 nếu tìm thấy
> cuBLAS/cuDNN (sidecar tự fallback CPU). Marian ONNX chỉ dùng GPU khi ép
> `EZ_TRANSLATE_ENGINE=marian`. `useGpuAcceleration` là cờ legacy, không điều khiển OCR.

## Tài nguyên (máy yếu)

`detectOcrResourcePlan()` chọn profile một lần lúc khởi động:

| | Low | Balanced | High |
|---|---|---|---|
| Điều kiện | ≤4 core, RAM <8GB, hoặc VRAM <2GB | còn lại | ≥12 core, RAM ≥15GB, VRAM ≥4GB |
| OCR settle / cooldown | 40 / 180 ms | 20 / 90 ms | 15 / 60 ms |
| Motion window | 140 ms | 100 ms | 80 ms |
| Capture FPS cap | 20 | 30 | 30 |
| Line cache | 1024 | 2048 | 4096 |
| OCR box cache/vùng | 4 | 8 | 12 |
| Translation cache | 1500 | 3000 | 5000 |
| CPU rec workers | 2 | 2-4 | 2-4 |
| GPU recMaxWidth | theo VRAM | theo VRAM | theo VRAM |

Override để test/A-B:

- `EZ_RESOURCE_PROFILE=low|balanced|high`
- `EZ_OCR_GPU_ADAPTER=<DXGI index>` (test fallback bằng index sai)
- `EZ_OCR_DET_THREADS`, `EZ_OCR_REC_THREADS`, `EZ_OCR_REC_WORKERS`,
  `EZ_OCR_INTER_THREADS`, `EZ_OCR_REC_MAX_WIDTH`
- `EZ_OCR_PROGRESSIVE=0|1`, `EZ_OCR_PROGRESSIVE_CHUNK=<2..12>`
- `EZ_OCR_REC_HIGHPRIORITY=1` (A/B worker OCR ưu tiên cao khi CPU đang bận)
- `EZ_OCR_REC_INT8=0` (quay lại model recognition fp32 trên CPU)
- `EZ_TRANSLATE_THREADS`

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

## Thiết bị OCR theo stage (Auto)

Settings → OCR có hai lựa chọn độc lập:

- **Detection**: `auto | cpu | directml | cuda`
- **Recognition**: `auto | cpu | directml | cuda`

Fallback:

```text
auto/cuda: CUDA → DirectML → CPU
directml: DirectML → CPU
cpu: CPU
```

Provider thực tế hiển thị dạng `Det:<provider>/Rec:<provider>` trong status/log.
CUDA OCR cần gói ONNX Runtime CUDA cài thêm; build DirectML mặc định sẽ fallback
DirectML rồi CPU. Mặc định vẫn là `auto`.

### Gói CUDA OCR optional

Base app chỉ có ONNX Runtime DirectML. Trong Settings → OCR, chọn CUDA sẽ hỏi
tải gói ONNX Runtime CUDA (~750 MB–1 GB). Script cài vào
`%LOCALAPPDATA%\EZTeam\EZ-Translator\cuda`, tạo profile app riêng và marker.
Lần mở sau, launcher ẩn trong `main.cpp` sẽ chạy profile CUDA nếu setting vẫn
là CUDA; nếu gói thiếu/lỗi, app quay lại DirectML/CPU. Không cần chọn launcher
thủ công.
