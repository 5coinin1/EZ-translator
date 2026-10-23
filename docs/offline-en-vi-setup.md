# Dịch offline Anh - Việt

Chạy `scripts/setup_offline_en_vi.ps1` từ PowerShell với Python 3.12 và pip đã cài.
Nếu Python nằm ở đường dẫn khác, truyền `-PythonExecutable "C:\path\to\python.exe"`.
Script tạo `.venv` và tải model vào `models/ct2-en-vi-int8`; hai thư mục này không được commit.
Khởi động lại app sau khi cài. Chế độ dịch **Tự động** ưu tiên CT2 trước Google; log cần có
`[Translate] engine ct2 sẵn sàng` và `offline = 1`.

Model CT2 lấy từ [Sams200/opus-mt-en-vi](https://huggingface.co/Sams200/opus-mt-en-vi),
bản chuyển đổi của [Helsinki-NLP/opus-mt-en-vi](https://huggingface.co/Helsinki-NLP/opus-mt-en-vi).
Kiểm tra điều khoản giấy phép trên trang model trước khi đóng gói phân phối.
Script sửa `layer_norm_epsilon` từ `null` thành `1e-5` vì CT2 không nạp được giá trị null.

Kiểm tra: `build/translate_smoke.exe` phải ghi `engine = ct2 int8` vào
`translate_smoke.out.txt`. Nếu chạy bản release đặt ngoài cây source, đặt biến
`EZ_MODELS_DIR` và `EZ_PYTHON` trỏ tới model và Python có `ctranslate2`, `sentencepiece`.
