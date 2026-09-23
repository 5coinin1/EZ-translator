# overlay/

Lớp phủ trong suốt hiển thị chữ (OCR / bản dịch) đè lên cửa sổ đích.

## TextOverlay

`QWidget` top-level:
- **Trong suốt**, không viền, luôn trên cùng (`WA_TranslucentBackground`).
- **Click-through**: `Qt::WindowTransparentForInput` + `WA_TransparentForMouseEvents`
  → chuột/bàn phím vẫn xuyên qua để dùng ứng dụng gốc.
- Không cướp focus (`WA_ShowWithoutActivating`, `Qt::NoFocus`).

Cách hoạt động:
1. `showOverTarget(clientLogicalRect)` — đặt overlay lên đúng vùng client của
   cửa sổ đích (toạ độ logical của Qt).
2. `setTextBoxes(boxes, frameSize)` — box theo toạ độ frame capture.
3. `paintEvent` map tỉ lệ frame → widget, che chữ gốc bằng nền đặc rồi vẽ chữ lên.

Đây là bản cơ bản: hiển thị **text OCR** (chưa dịch). Khi có tầng `translation/`
chỉ cần thay text truyền vào — không cần đổi overlay.

## Tích hợp

`AppController::onCaptureFrame`:
- OCR từng vùng đã đổi → map box từ toạ độ ROI sang toạ độ frame.
- Lưu box theo `regionId` (`m_regionTextBoxes`) để overlay luôn đầy đủ kể cả khi
  vùng không đổi ở frame hiện tại.
- `updateOverlay()` gom mọi vùng rồi đặt lên cửa sổ đích.

Overlay tự ẩn khi dừng dịch, khi xoá vùng, hoặc khi không còn box.
