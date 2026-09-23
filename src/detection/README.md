# detection/

Change detection (bo qua frame tinh) va text region detection (tim vi tri co chu). Nhan ROI image tu region/.

## Thanh phan

- `ChangeDetector` — dHash 64-bit + khoang cach Hamming, key theo `regionId`.
  Vung khong doi bi bo qua de khong OCR/dich lai (nguyen tac #2).
- `TextBox` — struct hop bao vung chu (rect + confidence).
- `ITextDetector` — interface thuan ao cho bo phat hien vung chu.
- `HeuristicTextDetector` — integral image + nguong thich nghi + connected-components.
  Khong can model AI, phu hop text UI do tuong phan ro.
- `RecursiveXYCut` — gom nhieu `TextBox` thanh cac block van ban (cot/khoi).

## Tich hop

`AppController::onCaptureFrame`:
1. `RegionManager` crop ROI -> `RegionFrame[]`.
2. `ChangeDetector` loc tung vung khong doi.
3. `HeuristicTextDetector` tim text box trong ROI thay doi.
4. `RecursiveXYCut` gom box -> block.

Buoc tiep theo: chuyen `regionFrame.image` + boxes/blocks sang `ocr/`.

Test: `tests/test_detection.cpp`.
