# translation/

Dịch văn bản đã OCR sang tiếng Việt. Tách **cache** / **engine** / **ghép đoạn**.

## Thành phần

### `ITranslator` — interface engine
`translate(lines, source, target) -> QStringList` (rỗng = dòng đó lỗi). Tách interface
để thay engine mà không đụng pipeline.

### `GoogleTranslator` — engine chính (online)
Endpoint công khai `translate.googleapis.com/translate_a/single` (`client=gtx`).
Không cần model, chạy được ngay; gọi **đồng bộ** (đợi mạng) nên chỉ dùng cho unit mới.

### `MyMemoryTranslator` — engine dự phòng (online)
`api.mymemory.translated.net`. Dùng khi Google bị chặn/giới hạn (429). Chất lượng thấp
hơn nhưng độc lập hạ tầng nên pipeline vẫn có bản dịch.

### `TranslationCache` — cache theo nội dung (L1 RAM)
Key = (nguồn, đích, text). Cuộn qua lại không phải dịch lại. Giới hạn LRU.

### `DocumentTranslator` — ghép đoạn + cache
- Tách văn bản assemble theo **đoạn** (ngăn bởi dòng trống); đếm **số dòng nguồn** mỗi
  đoạn (để tầng trên đặt overlay).
- Đoạn đã có trong cache → dùng lại; chỉ đoạn **mới** mới gọi engine (một lần cho cả lô).
- Trả `TranslatedDocument{sourceParagraphs, paragraphs, lineCounts, complete, newTranslations}`.

## Tích hợp

`AppController` chạy dịch trên **worker thread** (`TranslationWorker`, `QThread`) để
không chặn UI:

1. Khi OCR vừa chạy (vùng đã settle): assemble → hiện NGAY **text nguồn** (overlay
   không trống) → gửi yêu cầu dịch `(regionId, text, token)` bằng QueuedConnection.
2. Worker dịch (cache + engine) rồi phát `translated(regionId, token, doc)`.
3. Main thread nhận kết quả (bỏ qua nếu `token` cũ) → đặt mỗi đoạn lên **span các
   dòng nguồn** → cập nhật overlay.

`token` tăng dần mỗi yêu cầu nên kết quả của vùng đã đổi nội dung sẽ bị bỏ qua.
Engine online (Google → MyMemory) sống trong worker thread.

## Kiểm tra nhanh

```
build\Release\translate_smoke.exe
EZ_SRC=zh EZ_TGT=vi build\Release\translate_smoke.exe
```

Test: `tests/test_translation.cpp`.

## Hướng mở rộng

Engine **offline** (Marian ONNX / CTranslate2) chỉ cần implement `ITranslator` và gán
vào `AppController` — không đổi phần còn lại.
