# Translator — Dịch game / truyện theo thời gian thực

Ứng dụng chạy trên Windows, đọc chữ trên màn hình (game, trình đọc truyện, khung chat…)
rồi dịch sang tiếng Việt và hiện lên **ngay tại chỗ**, để người dùng gần như không thấy bản gốc.

---

## 1. Mục tiêu

- Đọc chữ trên một **cửa sổ** mà người dùng chỉ định rồi dịch sang tiếng Việt.
- Hiện bản dịch **đè lên đúng vị trí** chữ gốc, giữ nguyên bố cục (xuống dòng, cách dòng, khoảng trắng).
- Che chữ gốc để người dùng chỉ thấy bản dịch.
- Người dùng **tự khoanh vùng** cần dịch (gọi là *vùng dịch*), thay vì để ứng dụng đoán mò cả màn hình.
- Mục tiêu cảm nhận: **không thấy độ trễ** — bản gốc biến mất ở khung hình đầu tiên, chữ dịch hiện ra gần như tức thì.

---

## 2. Những vấn đề cần giải quyết

1. **Dịch xong bị lệch chữ / loạn chữ** khi màn hình thay đổi (cuộn, đổi giao diện).
2. **Phải chờ lâu mới có bản dịch**, trong lúc chờ vẫn thấy chữ gốc.
3. **Chữ dịch bị đổi kiểu chữ / cỡ chữ / vị trí**, nhìn không khớp với bản gốc.
4. **Tốn kém và chậm** nếu câu nào cũng gọi dịch qua mạng.
5. **Một ứng dụng có nhiều màn hình khác nhau** (menu, khung chat, màn chơi…) cần xử lý riêng.
6. **Không thao tác được vào ứng dụng gốc** khi lớp dịch hiện lên.
7. **Nội dung dài tải dần** khi cuộn (truyện/khung chat), cần nhớ nội dung và vị trí đã dịch.

---

## 3. Cách ứng dụng giải quyết

1. **Người dùng tự khoanh vùng cần dịch** → chỉ dịch trong vùng đó, không lệch khi màn hình đổi.
2. **Che trước, dịch sau**: vừa nhận ra vùng có chữ là che chữ gốc ngay, chữ dịch hiện lên sau
   → người dùng không thấy bản gốc, và không thấy "trống" quá lâu.
3. **Ghi nhớ kiểu chữ của từng vùng** (kiểu chữ, cỡ chữ, màu chữ) để lần sau hiện giống hệt;
   người dùng có thể chỉnh lại.
4. **Nhớ những gì đã dịch**: câu nào dịch rồi thì lần sau lấy lại ngay, không dịch lại
   → vừa nhanh vừa tiết kiệm. Có thể dịch **ngay trên máy** (không cần mạng) để không phụ thuộc, không tốn phí.
5. **Mỗi màn hình là một "hồ sơ"** gồm nhiều vùng dịch; ứng dụng tự nhận biết đang ở màn nào để dùng đúng hồ sơ.
6. **Lớp hiện chữ là lớp trong suốt, chuột và bàn phím vẫn xuyên qua** để dùng ứng dụng gốc bình thường.
7. **Nhớ nội dung đã dịch theo từng vùng** để khi cuộn lên/xuống thì hiện lại ngay, không phải dịch lại.

---

## 4. Ứng dụng gồm những phần nào

- **Chọn cửa sổ cần dịch**: liệt kê các cửa sổ đang mở để người dùng chọn một cái.
- **Chọn ngôn ngữ**: ngay trên màn hình chính, có nút hoán đổi nguồn ↔ đích; đổi ngôn
  ngữ sẽ áp dụng cho cả OCR lẫn engine dịch và được lưu lại cho lần chạy sau.
- **Khung vẽ vùng dịch**: trên ảnh chụp cửa sổ đã chọn, người dùng khoanh các vùng cần dịch
  (hình chữ nhật hoặc hình bất kỳ), đặt tên và chọn kiểu cho từng vùng.
- **Tự tìm vùng chữ**: gợi ý sẵn các vùng có chữ để người dùng giữ/bỏ, đỡ phải vẽ tay hoàn toàn.
- **Đọc chữ (nhận dạng chữ)**: nhận ra chữ trong từng vùng.
- **Che chữ gốc**: tô kín vùng cần dịch bằng màu nền để chữ gốc không còn nhìn thấy.
- **Dịch**: chọn engine trong *Cài đặt → Dịch thuật*
  - *Tự động*: dùng model trên máy nếu có, không thì Google.
  - *Google*: endpoint miễn phí, hoặc Cloud Translation API khi nhập API key.
  - *AI / LLM*: endpoint OpenAI-compatible (OpenAI, OpenRouter, DeepSeek, Ollama…),
    model, API key và **system prompt** để AI hiểu ngữ cảnh game/truyện.
  - *Offline*: chỉ dịch trên máy, không gửi dữ liệu ra mạng.
- **Hiện chữ dịch**: lớp phủ trong suốt, hiện đúng vị trí, đúng bố cục, có thể chỉnh chữ.
- **Nhớ nội dung đã dịch**: lưu lại câu đã dịch để dùng lại, tránh dịch trùng.
- **Quản lý nhiều màn hình**: lưu theo hồ sơ để lần sau mở lên dùng tiếp.

---

## 5. Cách ứng dụng hoạt động (dễ hiểu)

```
Chụp cửa sổ  →  Kiểm tra có gì thay đổi không?  →  (nếu không đổi thì bỏ qua)
      │
      ▼
Đọc chữ trong từng vùng
      │
      ├─► Che chữ gốc ngay (để không lộ bản gốc)
      │
      ▼
Dịch:  nhớ sẵn (nếu đã dịch) → nếu chưa thì dịch
      │
      ▼
Hiện chữ dịch lên đúng vị trí
```

Điểm quan trọng: **che bản gốc là việc làm trước tiên**, còn dịch và hiện chữ đến sau.
Nhờ vậy dù việc dịch mất một chút thời gian, người dùng vẫn không thấy bản gốc và không thấy "chờ".

---

## 6. Nguyên tắc khi làm

1. **Che trước, dịch sau** — tạo cảm giác không có độ trễ.
2. **Không làm gì khi màn hình không đổi** — phần lớn thời gian màn hình đứng yên, bỏ qua để nhẹ máy.
3. **Nhớ trước khi dịch** — thử lấy từ nơi đã lưu trước, chỉ dịch khi thật sự cần.
4. **Xử lý ngay trên cửa sổ đích** — hạn chế copy qua lại để nhanh và mượt.
5. **Không cố thắng tốc độ máy bằng mẹo code** — thay vào đó tập trung che độ trễ và hiện chữ dần.

---

## 7. Yêu cầu (Windows 10/11 x64)

| Thành phần | Ghi chú |
|---|---|
| **Visual Studio 2022/2026 Build Tools** (MSVC, Desktop C++) | hoặc VS bản đầy đủ |
| **Qt 6.11.2 msvc2022_64** (Core, Gui, Widgets, Network, Concurrent, Test) | tự dò `C:\Qt\6.*\msvc*`; hoặc set `QTDIR` |
| **CMake ≥ 3.24** | có sẵn trong VS, hoặc `winget install Kitware.CMake` |
| **PowerShell 7 (`pwsh`)** | để chạy script tải dependency (Win11 có sẵn; Win10: `winget install Microsoft.PowerShell`) |
| **Python 3.10+** (tuỳ chọn) | cho engine dịch offline CT2; thiếu thì app vẫn chạy, dịch qua Google |

---

## 8. Chạy lần đầu (một lệnh)

```cmd
setup.bat
```

`setup.bat` làm đủ 3 việc: tải dependency (ONNX Runtime + model OCR + model dịch CT2), cài
gói Python cho sidecar (`ctranslate2`, `sentencepiece`), rồi build Release. Sau đó:

```cmd
run.bat
```

Từng bước thủ công:

```powershell
pwsh -File scripts/fetch_deps.ps1     # ONNX Runtime + model OCR (+ CT2 nếu release có)
pwsh -File scripts/fetch_python.ps1   # ctranslate2 + sentencepiece (tuỳ chọn)
build.bat                              # build Release + windeployqt
```

### Model dùng trong app

| Loại | Model | Nguồn |
|---|---|---|
| OCR dò dòng | `ch_PP-OCRv4_det_infer.onnx` | RapidOCR (HF) — tự tải, kiểm SHA256 |
| OCR chữ Anh | `en_PP-OCRv4_rec_mobile.onnx` + `en_dict.txt` | RapidOCR / PaddleOCR |
| OCR chữ Trung | `ch_PP-OCRv4_rec_infer.onnx` + `ppocr_keys_v1.txt` | RapidOCR / PaddleOCR |
| OCR chữ Nhật | `japan_PP-OCRv4_rec_infer.onnx` + `japan_dict.txt` | cycloneboy (HF) |
| Dịch en→vi | `models/ct2-en-vi-int8` | GitHub Release `models-ct2.zip` |
| Dịch zh→vi | `models/ct2-zh-vi` (HachimiMT-60) | GitHub Release `models-ct2.zip` |
| Dịch ja→vi | `models/ct2-ja-vi` (hirashiba-medium) | GitHub Release `models-ct2.zip` |

Thiếu model dịch CT2 thì app vẫn chạy: engine "Tự động" rơi xuống Google (cần mạng).

---

## 9. Chạy

Double-click `run.bat`, hoặc:

```cmd
run.bat
```

Hoặc chạy trực tiếp `build\Release\EZTranslator.exe` (Qt runtime đã được `windeployqt` chép cạnh exe).
