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
2. `setBoxes(boxes, frameSize)` — box (`OverlayBox{rect,text}`) theo toạ độ frame.
3. `paintEvent` map tỉ lệ frame → widget, che chữ gốc bằng nền đặc rồi vẽ chữ lên.

`OverlayBox` là kiểu chung (rect + text), nên overlay không phụ thuộc kiểu OCR: tầng
trên truyền vào text OCR hoặc **bản dịch** tuỳ tình huống.

## Tích hợp

`AppController::onCaptureFrame`:
- OCR vùng đã settle → assemble thành đoạn → dịch theo đoạn (cache) →
  đặt mỗi đoạn lên span các dòng nguồn của nó.
- Lưu box theo `regionId` (`m_regionBoxes`) để overlay luôn đầy đủ kể cả khi vùng
  không đổi / bản dịch còn đang chờ ở frame hiện tại (không bao giờ để trống).
- `updateOverlay()` gom mọi vùng rồi đặt lên cửa sổ đích.

Overlay tự ẩn khi dừng dịch, khi xoá vùng, hoặc khi không còn box.
