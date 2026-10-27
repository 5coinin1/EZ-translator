# Báo cáo Phase 6 — Large ROI & Smart-cover

## Trạng thái

**Đạt một phần**

Đã đưa pipeline detect-before-recognize vào runtime dưới cờ A/B; đường tiled cũ và smart-cover hiện tại vẫn là fallback an toàn.

## Đã làm

- `EZ_OCR_TILE_DETECT_ONCE=1` khiến ảnh lớn chạy detector theo tile, map box về tọa độ ảnh gốc, merge/dedup trước rồi mới gọi recognizer.
- Recognition dùng crop theo box trên ảnh gốc, tránh recognize lặp box ở vùng overlap của tile.
- Timing tổng vẫn cộng đủ detector tile, recognizer, cache, alternate model và tổng pipeline.
- `EZ_OCR_TILE_DETECT_ONCE=0` giữ path cũ để rollback tức thì nếu workload có accuracy regression.
- Smart-cover tiếp tục được dựng trên OCR worker, cache plate có giới hạn theo vùng và tổng budget RAM.

## Kiểm tra

- `ez_ocr` build thành công.
- CTest pass `3/3`.
- `ocr_bench` CPU synthetic 1920x1080, 3 lượt: tiled baseline median khoảng `1144 ms`, detect-once median khoảng `990 ms` (xấp xỉ 13,5% nhanh hơn).
- Hai path đều trả 6 box trên workload synthetic này; đây chỉ là proxy sanity check, chưa thay thế CER/recall trên ảnh game thật.

## Giới hạn

Merge box không thể chứng minh giữ nguyên reading order/CJK/overlap nếu không có dataset thực. Vì vậy path mới là opt-in, chưa đổi mặc định; số đo trên chỉ là tín hiệu tốt ban đầu, chưa kết luận p95/accuracy tổng quát.

