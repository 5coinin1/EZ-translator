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

`AppController::onCaptureFrame` (chỉ khi vùng đã settle, tức OCR vừa chạy):
1. `OcrTextAssembler::assembleDocument` → text + rect từng dòng.
2. `DocumentTranslator::translate` → bản dịch từng đoạn (cache lo phần đã có).
3. Đặt mỗi đoạn lên **span các dòng nguồn** của nó → `OverlayBox` cho `TextOverlay`.
4. Đoạn chưa dịch xong → tạm hiện **text nguồn** (không bao giờ để trống).

Dịch chạy trên main thread và **đồng bộ** cho unit mới → có thể chặn UI một nhịp khi
xuất hiện đoạn mới. Hướng cải tiến: đẩy sang worker thread + dịch bất đồng bộ.

## Kiểm tra nhanh

```
build\Release\translate_smoke.exe
EZ_SRC=zh EZ_TGT=vi build\Release\translate_smoke.exe
```

Test: `tests/test_translation.cpp`.

## Hướng mở rộng

Engine **offline** (Marian ONNX / CTranslate2) chỉ cần implement `ITranslator` và gán
vào `AppController` — không đổi phần còn lại.
