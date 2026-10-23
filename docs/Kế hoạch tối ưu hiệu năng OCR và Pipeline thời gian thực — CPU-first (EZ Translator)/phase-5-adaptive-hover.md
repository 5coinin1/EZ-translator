# Báo cáo Phase 5 — Adaptive Hover Experiment

## Trạng thái

**Đạt một phần**

Đã triển khai đầy đủ đường A/B adaptive hover, nhưng chưa bật mặc định vì repo chưa có bộ ảnh ground-truth để chốt accuracy.

## Đã làm

- `EZ_HOVER_ADAPTIVE_BAND=1` bật thang band `1200x96` → `1200x260` → `1600x300` → `2200x340` → `3000x380`.
- OCR worker phát hiện line chạm mép band thật thay vì luôn emit `clippedByBand=false`.
- Probe final tự retry ở stage lớn hơn; prefetch không retry để không tăng CPU khi rê chuột.
- Log `[HOVER_BAND]` ghi stage, kích thước và tọa độ band phục vụ A/B.
- Baseline cũ vẫn giữ nguyên khi biến môi trường không bật.

## Kiểm tra

- `ez_ocr`, `ocr_bench` và target app compile thành công tới bước link.
- CTest pass `3/3`.
- Chưa có benchmark accuracy dòng dài/mép cửa sổ/EN-CJK trên ground-truth.

## Giới hạn

Chưa tuyên bố adaptive band nhanh hơn hoặc chính xác hơn baseline. Khi cần A/B: chạy cùng workload với `EZ_HOVER_ADAPTIVE_BAND=0` và `1`, so p50/p95, số retry, box recall và CER.

