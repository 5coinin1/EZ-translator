# translation/

Dịch văn bản đã OCR theo cặp ngôn ngữ chọn trên màn hình chính (mặc định en → vi).
Tách **cache** / **engine** / **ghép đoạn**.

## Thành phần

### `ITranslator` — interface engine
`translate(lines, source, target) -> QStringList` (rỗng = dòng đó lỗi). Tách interface
để thay engine mà không đụng pipeline.

### `GoogleTranslator` — engine Google (online)
- **Có API key** (`AppSettings::googleApiKey`) → Cloud Translation API v2 chính thức,
  batch nhiều câu trong một request và giải mã HTML entity.
- **Không có key** → endpoint công khai `translate.googleapis.com/translate_a/single`
  (`client=gtx`). Không cần cấu hình nhưng có thể bị giới hạn.
- Gọi **đồng bộ** (đợi mạng) nên chỉ dùng cho unit mới.

### `AiTranslator` — engine AI/LLM (online)
Gọi endpoint tương thích OpenAI Chat Completions (`POST /v1/chat/completions`), dùng
được với OpenAI, OpenRouter, DeepSeek, Groq, Ollama, LM Studio, vLLM…
- `systemPrompt` của người dùng được gửi ở message `system` để model hiểu ngữ cảnh.
- App tự thêm quy tắc trả về **mảng JSON** cùng thứ tự; engine có fallback parse
  mảng, text thuần hoặc danh sách đánh số.
- Batch tối đa 8 đoạn/lần để có ngữ cảnh và giảm số request.

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
- Sau khi dịch, chạy **hậu xử lý** (xem dưới) trước khi lưu cache.

### `TranslationPost` — hậu xử lý bản dịch
- `collapseRepeats`: gộp từ/cụm bị model sinh lặp ("đau đau đau đau đớn"); giữ láy thật.
- `applyPunctuation`: khôi phục dấu câu/đánh dấu hội thoại của bản gốc ("- Hello!").
- `isUsableTranslation`: lọc output rác (echo, ký tự lặp, quá ngắn/quá dài) — rác bị coi
  như chưa dịch, để lần sau thử lại và tạm hiện bản gốc.

### `LanguageDetect` — nhận diện ngôn ngữ nguồn
Đoán theo **hệ chữ Unicode** (en/zh/ja/ko/ru/ar/he/th), rẻ và tất định. Dùng khi
`sourceLanguage = "auto"`: detect từ text OCR rồi cấu hình lại worker dịch.

## Tích hợp

`AppController` chạy dịch trên **worker thread** (`TranslationWorker`, `QThread`) để
không chặn UI. Engine được chọn trong *Cài đặt → Dịch thuật* và truyền sang worker
qua `TranslationConfig` (`auto` / `google` / `ai` / `offline`):

1. Khi OCR vừa chạy (vùng đã settle): assemble → hiện NGAY **text nguồn** (overlay
   không trống) → gửi yêu cầu dịch `(regionId, text, token)` bằng QueuedConnection.
2. Worker dịch (cache + engine) rồi phát `translated(regionId, token, doc)`.
3. Main thread nhận kết quả (bỏ qua nếu `token` cũ) → đặt mỗi đoạn lên **span các
   dòng nguồn** → cập nhật overlay.

`token` tăng dần mỗi yêu cầu nên kết quả của vùng đã đổi nội dung sẽ bị bỏ qua.
Engine đang chọn (offline / Google / AI) sống trong worker thread; cấu hình API key,
model và system prompt được lưu bằng `QSettings` và nạp lại ở lần chạy sau.

## Kiểm tra nhanh

```
build\Release\translate_smoke.exe
EZ_SRC=zh EZ_TGT=vi build\Release\translate_smoke.exe
```

Test: `tests/test_translation.cpp`.

## Thêm engine mới

Implement `ITranslator` rồi khởi tạo trong `TranslationWorker::configure` theo giá
trị `TranslationConfig::engine`; không cần đụng tới pipeline OCR/overlay.
