# Kế hoạch tối ưu hiệu năng OCR và Pipeline thời gian thực — CPU-first (EZ Translator)

Thư mục này lưu báo cáo triển khai theo từng phase của kế hoạch tại:

`docs/ocr-performance-optimization-plan.md`

## Quy ước báo cáo

Sau mỗi phase được triển khai, tạo một file Markdown theo mẫu:

`phase-N-ten-phase.md`

Mỗi báo cáo cần ghi rõ:

- Phạm vi phase và ngày triển khai.
- Các file đã thay đổi.
- Những thay đổi thực sự đã thực hiện.
- Phần chưa triển khai hoặc còn chờ benchmark.
- Kết quả build/test/benchmark.
- Rủi ro còn lại và cách rollback.
- Trạng thái: `Đạt`, `Đạt một phần`, hoặc `Chưa đạt`.

Không ghi một đề xuất trong plan thành kết quả đã hoàn thành nếu chưa có code và bằng chứng kiểm tra tương ứng.

## Báo cáo hiện có

- [Phase 0 — Correctness & Concurrency Audit](phase-0-correctness-concurrency-audit.md)
- [Phase 1 — Instrumentation & CPU Baseline](phase-1-instrumentation-cpu-baseline.md)
- [Phase 2 — Bounded Scheduling & Latest-wins cho Hover và Region — Đạt](phase-2-bounded-scheduling-hover.md)
- [Phase 3 — CPU Thread Budget — Đạt](phase-3-cpu-thread-budget.md)
- [Phase 4 — Translation QoS — Đạt](phase-4-translation-qos.md)
- [Phase 5 — Adaptive Hover Experiment — Đạt một phần](phase-5-adaptive-hover.md)
- [Phase 6 — Large ROI & Smart-cover — Đạt một phần](phase-6-large-roi-smart-cover.md)
- [Phase 7 — Capture & Memory — Đạt](phase-7-capture-memory.md)
- [Phase 8 — CPU Model Evaluation — Đạt một phần](phase-8-cpu-model-evaluation.md)
