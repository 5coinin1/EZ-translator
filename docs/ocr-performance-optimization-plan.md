# Kế hoạch tối ưu hiệu năng OCR và Pipeline thời gian thực — CPU-first (EZ Translator)

## 1. Phạm vi và độ tin cậy

Tài liệu này cập nhật plan cũ theo working tree nhánh `tu`, HEAD `d166082c55284edd6490f5e41cdba991854ddd82`. Nhánh mục tiêu trong yêu cầu là `develop-5coinin1`, commit tham chiếu `9cba5222d1b0f0ced16492310edb7872550b0706`; không trùng nhánh/HEAD hiện tại. Source working tree là nguồn sự thật; không checkout. Working tree sạch trước lần chỉnh tài liệu này.

Máy đích: Windows 10/11 x64, i5-12500H (4 P-core + 8 E-core, 16 logical threads), 16 GB RAM, Intel Iris Xe, không có GPU rời. ONNX Runtime CPU Execution Provider là baseline bắt buộc. DirectML, FP16, CUDA và GPU rời chỉ là nhánh tùy chọn. WGC/GDI là backend capture, độc lập với OCR execution provider.

Nhãn độ tin cậy: **Verified in Source** = đã thấy trong code, không phải số đo; **Observed in Benchmark/Log** = có thực nghiệm/log kèm cấu hình và workload; **Potential Risk** = nguy cơ chưa tái hiện; **Hypothesis** = lợi ích cần đo; **Proposed Optimization** = chưa triển khai. Không coi mục tiêu hiệu năng là kết quả đã đạt.

Mục tiêu giữ lại từ plan gốc: giảm chênh lệch phản hồi CPU/GPU, giữ accuracy, RAM ổn định, không để request cũ chặn hoặc làm sai kết quả mới. Không cần CPU nhanh ngang mọi GPU.

## 2. Source Code Assessment

### 2.1 Hai đường xử lý thực tế

- **Verified in Source:** WGC dùng helper process/shared memory; AppController fallback GDI khi WGC khởi tạo hoặc không có frame đầu. Hai capture backend có `framePending` để hạn chế frame chưa tiêu thụ. GDI fallback không chứng minh nguyên nhân OCR chậm.
- **Verified in Source:** `AppController::startOcr` tạo worker vùng; `ensureHoverOcr` tạo worker hover riêng khi cần. Mỗi worker có engine/session riêng. App chọn CPU khi không có GPU rời phù hợp. OCR không chạy trực tiếp trên UI thread.
- **Verified in Source:** vùng đi qua dHash/change detection, box cache, scroll profile, `OcrScheduler`, region OCR, translation và overlay/smart-cover. Scheduler runtime mặc định `settleMs=15`, `minRunIntervalMs=60`, `maxRunsPerTick=1`; `src/ocr/README.md` còn ghi 130/600, không dùng làm source of truth. Có code scroll reuse, nhưng không mặc định đường incremental OCR đang được gọi trong tick hiện tại.
- **Verified in Source:** hover prefetch khoảng 100 ms, final khoảng 120 ms. `sendHoverProbe` dùng Qt queued invocation; `m_hoverProbes` tối đa 8 chỉ giới hạn metadata, không giới hạn `QMetaCallEvent` trong worker. Token cuối bỏ kết quả stale khi trả về nhưng không bỏ inference đã xếp hàng. Fixed band 1200×96; `OcrWorker::probeHover` emit `clippedByBand=false`, nên nhánh mở rộng theo clipping không chạy.
- **Verified in Source:** `OcrWorker::recognizeRegion` emit boxes rồi dựng smart-cover plate trên cùng OCR worker. `m_pendingRequests` là counter điều kiện hóa plate, không phải bounded queue/cancellation. Plate đã ngoài UI thread.

### 2.2 OCR, dịch và những tối ưu đã tồn tại

- **Verified in Source:** PP-OCRv4 có detector/recognizer tách biệt, line cache, CPU parallel recognition và GPU dynamic batch/FP16 khi cấu hình thích hợp. Mặc định `detLimitSide=640`, `recThreads=1`, `disableRecBatch=true`; không mặc định CPU batching sẽ nhanh hơn.
- **Verified in Source:** ảnh từ 1600×900 được chia 2×2; từng tile gọi `recognizeProcessed()` gồm detect và recognize, rồi mới dedup. Non-tiled path có alternate recognizer khi confidence thấp. `lastTimings()` lấy timing cuối, chưa phản ánh tổng nhiều tile/alternate pass.
- **Verified in Source:** `TranslationWorker` riêng, interactive path, cache, Google/MyMemory/offline và fallback đã có. Google gọi tuần tự từng unit với timeout; chưa thấy circuit breaker 429. CTranslate2 sidecar có thread budget riêng và có thể tranh CPU OCR. Giữ source anchoring/token khi sửa scheduling.
- **Verified in Source:** có `ocr_bench`, `hover_probe_test`, `frame_noise_probe`, `scroll_cache_bench`, `plate_bench`, `overlay_probe`. Các smoke/synthetic tools này chưa phải benchmark end-to-end trên game/capture thật và chưa chứng minh hết race/stale/backpressure.

### 2.3 Rủi ro và ưu tiên

| Ưu tiên | Nhận định | Phân loại | Hành động |
| --- | --- | --- | --- |
| P0 | Tới 6 CPU recognition threads ghi `LineCache::insert()` vào `QHash`/`QQueue` chung không khóa | Verified in Source | Gom output từng index; chỉ insert cache sau `join`; stress test |
| P0 | Map hover không chặn Qt queue; stale token lọc khi đã OCR xong | Verified in Source | Bounded mailbox thật và đo queue wait |
| P0 | Region signal chỉ trả `regionId`/boxes; geometry/hash trong map có thể bị frame mới ghi đè; `m_ocrGeneration` chỉ guard `loaded` | Potential Risk | Request snapshot/generation và stale tests |
| P1 | Region plate cùng worker; pending counter không coalesce | Verified in Source | Đo plate/queue trước khi tách lịch |
| P1 | Tiled recognition trước dedup, timing thiếu aggregate | Verified in Source | Thử detect-merge-recognize, chưa kết luận sẽ nhanh hơn |
| P1 | Hai OCR workers, ORT threads, parallel rec và offline translation có thể tranh CPU | Potential Risk | Đo contention trên i5-12500H |
| P1 | Google 429/timeout có thể kéo dài từng unit trước fallback | Potential Risk | Đo network stall; thử breaker |
| P2 | Fixed hover band, clipping stage vô hiệu | Verified in Source | Chỉ A/B adaptive band sau baseline accuracy |
| P2 | Capture copy/WGC fallback làm chậm pipeline | Hypothesis | Đo capture riêng, phân loại fallback |

## 3. Benchmark và acceptance criteria

**Proposed Optimization:** trace qua OCR, translation, plate, overlay với `requestId`, session generation, target window, `regionId` hoặc hover token, frame sequence/hash, geometry snapshot. Dùng monotonic timestamps cho `captureReady`, `scheduled`, `queueEntered`, `ocrStarted`, `detFinished`, `recFinished`, `translateStarted/Finished`, `plateFinished`, `presented`. Phân biệt wall-clock với tổng model time, cộng nhiều tiles/alternate pass; tách queue, preprocessing, inference, decode, cache hit, số model runs, OCR-only và end-to-end. Sampling để logging không thành bottleneck; không ghi text riêng tư mặc định.

CSV/JSON gồm provider thật, image size, box count, queued/coalesced/dropped, CPU utilization, working set/peak RAM, cache hit, engine dịch, HTTP status và fallback. Đo p50/p95 cùng sample count, cold/warm, cache hit/miss, game chạy/không chạy, CPU-only và DirectML tùy chọn; ghi model/build/hardware và lưu raw data.

Workload tối thiểu: (1) hover dòng ngắn, (2) hover dòng dài, (3) rê chuột liên tục nhiều vị trí, (4) ROI 2–4 dòng thoại, (5) menu nhiều boxes, (6) truyện Trung/Nhật, (7) full-window 1080p, (8) scroll liên tục, (9) OCR + dịch offline, (10) dịch online timeout/429. Có ground truth CER/recall/reading order; chữ nhỏ/lớn, nền sáng/tối, frame tĩnh/động. Cùng input, nhiều lượt lặp, so sánh trước/sau từng PR.

Các số plan cũ (hover CPU p50 ≤250 ms/p95 ≤450 ms, GPU p95 ≤200 ms, ROI 1080p CPU p95 ≤1.000 ms, model nhanh hơn 20%, accuracy không giảm quá 1%) chỉ là **performance goals ứng viên**, chưa phải số đo hoặc ngưỡng đã xác thực. Phase 1 chốt acceptance từ baseline. Điều kiện chung: không stale/wrong geometry/starvation; queue thật hữu hạn; RAM dài hạn không tăng tuyến tính theo frame/request; CER/recall/đầu-cuối dòng không suy giảm ngoài ngưỡng chốt. Không đòi đúng hai model runs trong cả burst vì worker có thể hoàn thành nhiều request khi burst tiếp diễn.

## 4. Roadmap theo phụ thuộc

### Phase 0 — Correctness & Concurrency Audit (P0)

**Evidence/problem:** cache write đa luồng, region result thiếu context, generation chỉ guard `loaded`. **Scope/proposal:** threads chỉ ghi output riêng; cache insert sau `join`; audit Qt ownership/lifecycle, stop/start, đổi window/mode, plate và translation anchoring. Gắn context immutable vừa đủ cho region request/result, xác nhận session/window/frame trước khi dùng geometry/hash. **File dự kiến:** `src/ocr/OcrRecognizer.cpp`, `OcrWorker.*`, `src/app/AppController.*`, tests. **Test:** parallel rec stress/TSAN nếu hỗ trợ, worker chậm khi đổi frame/window, stop/start, stale plate/translation. **Acceptance:** không ghi shared cache đồng thời; kết quả cũ không dùng geometry/phiên mới; prerequisite build/test rõ ràng. **Risk/rollback:** thứ tự cache/ABI đổi; giữ khả năng rollback context/policy riêng. Phase 1 trace có thể song song nhưng kết luận tối ưu phụ thuộc Phase 0.

### Phase 1 — Instrumentation & CPU Baseline (P0)

**Evidence/problem:** timing thiếu queue, tile aggregate, alternate pass và plate. **Scope:** trace ở mục 3, benchmark đúng hai đường hover/region bằng tools hiện có và harness workload thực. **File dự kiến:** `src/ocr/OcrTypes.h`, `OcrEngine.cpp`, `OcrWorker.*`, `src/app/AppController.*`, `tools/*bench*`. **Test:** monotonic timestamps, nối đúng id, cache hit/miss, multi-tile aggregate. **Acceptance:** baseline p50/p95 từng stage, CPU/RAM/accuracy, queue depth/model runs cho 10 workload trên i5-12500H CPU EP, raw data/repro steps. **Risk/rollback:** logging overhead; sampling/feature flag, đo overhead.

### Phase 2 — Bounded Scheduling & Latest-wins (P0, sau 0–1)

**Evidence/problem:** `m_hoverProbes` không bound queued calls, region counter không coalesce. **Proposed Optimization:** hover mailbox 1 running + 1 newest pending; pending thay thế tại chỗ, chỉ enqueue khi worker rảnh; final ưu tiên prefetch; completion/error/stop đều nhả running. Region coalesce theo `regionId` + frame/hash/generation, fairness nhiều ROI, giữ scroll/box cache. Context Phase 0 quyết định invalidation; không giả định hủy ORT run đang chạy là an toàn. **File dự kiến:** `src/app/AppController.*`, `src/ocr/OcrWorker.*`, scheduler/tests. **Test:** fake worker chậm + 50 hover events, prefetch/final race, nhiều ROI, đổi frame/window, plate chậm. **Acceptance:** Qt queue hữu hạn, latest final được trình bày khi worker bắt kịp, không starvation, RAM không tăng theo số event. **Risk/rollback:** coalesce làm mất cập nhật; policy feature flag, giữ context P0.

### Phase 3 — CPU Thread Budget (P1, sau 1–2)

**Evidence/problem:** hai OCR sessions/workers, ORT intra-op, tới 6 rec threads, sidecar dịch có thể tranh CPU. **Hypothesis:** giới hạn tổng concurrency cải thiện p95/RAM. Thử detector/recognizer intra-op 2/2, 2/4, 4/4 và nhiều mức parallel-rec; tách intra-op, inter-op, số sessions, `std::thread`, translation threads và game load. Không giả định 16 logical threads đều tốt hoặc dùng chung `Ort::Env` tạo global pool. Giữ provider selection/fallback hiện có; profile Auto/CPU-balanced chỉ sau đo. **File dự kiến:** `src/ocr/OcrRuntime.*`, `OcrTypes.h`, `OcrRecognizer.cpp`, `src/app/AppController.cpp`, translation sidecar/config khi cần. **Test/acceptance:** A/B hover+ROI+offline dịch trên máy đích, p50/p95, CPU/RAM, UI/game responsiveness và accuracy không thoái hóa quá ngưỡng chốt. **Risk/rollback:** giảm throughput máy khác; giữ Auto/override cũ.

### Phase 4 — Translation QoS (P1, sau 1–2)

**Evidence/problem:** worker/cache/fallback/interactive đã có, Google tuần tự/timeout, chưa có breaker 429. **Proposed Optimization:** đo queue/network/cache; ưu tiên interactive có fairness, deadline hover, 429 backoff/circuit breaker TTL và fallback sớm; stale token/context trước render. Không coi hủy queued call là hủy network request đang chạy. **File dự kiến:** `src/translation/GoogleTranslator.cpp`, `MyMemoryTranslator.cpp`, `TranslationWorker.*`, `src/app/AppController.*`. **Test:** cache hit, timeout/429 lặp, nhiều ROI + hover, fallback, stale result. **Acceptance:** UI không treo, online stall bị giới hạn theo policy, không spam 429, offline không đói. **Risk/rollback:** breaker quá lâu; TTL/config, policy cũ.

### Phase 5 — Adaptive Hover Experiment (P2, sau 1–3)

**Evidence/problem:** fixed 1200×96 và `clippedByBand=false`; band nhỏ có thể từng bị rollback vì mất chữ, chưa có dữ liệu. **Hypothesis:** band/`detLimitSide` thích ứng giảm CPU time. A/B fixed baseline với candidate width/resolution, clipping thật, retry khi mép/no box/low confidence; recognize crop ảnh gốc. **File dự kiến:** `src/app/AppController.*`, `src/ocr/OcrWorker.*`, detector/tests. **Test:** dòng dài, chữ nhỏ, mép cửa sổ, text lân cận, EN/CJK. **Acceptance:** p50/p95 cải thiện có ý nghĩa mà không mất đầu/cuối dòng hay tăng CER/false positive ngoài ngưỡng. **Risk/rollback:** accuracy regression; feature flag fixed band.

### Phase 6 — Large ROI & Smart-cover (P2, sau 1)

**Evidence/problem:** 2×2 tile recognize trước dedup; plate cùng worker. **Hypothesis/proposal:** thử detect tile → map tọa độ ảnh gốc → merge/dedup → recognize; so sánh 1/2/4 tile, không tile ROI nhỏ. Không mặc định gộp box vào một ONNX Run nhanh trên CPU; batch CPU đang tắt. Đo plate wait/run và peak RAM trước khi tách lịch. **File dự kiến:** `src/ocr/OcrEngine.cpp`, `OcrDetector.*`, `OcrRecognizer.*`, `OcrWorker.cpp`, tests. **Test:** overlap, reading order, CJK novel, scroll anchoring, menu, plate không che sai. **Acceptance:** đúng tọa độ/accuracy, p95 và RAM có lợi ích được đo hoặc giữ baseline nếu không. **Risk/rollback:** merge mất box; feature flag tiled path cũ, không đưa plate về UI.

### Phase 7 — Capture & Memory (P2, sau 1)

**Evidence/problem:** WGC helper/shared memory và `framePending` đã có; cần phân loại fallback và đo copy/cache. **Proposed Optimization:** log helper missing/start/handshake/first-frame/unsupported/frame-invalid riêng; kiểm tra helper cạnh exe từng build/deployment (CMake đã có target dependency/output path, không kết luận packaging sai vô điều kiện). Đo capture-to-ROI, image copies, frame/box/line/plate caches theo bytes và RAM dài hạn. Chỉ thêm adaptive FPS khi trace chứng minh cần. **File dự kiến:** `src/capture/WgcWindowCapture.cpp`, `GdiWindowCapture.cpp`, `src/app/AppController.cpp`, packaging config nếu thực nghiệm cần. **Test:** WGC/GDI, cửa sổ tĩnh/động, resize/DPI, helper mất, chạy dài/scroll. **Acceptance:** fallback có lý do, frame đúng, RAM ổn định, capture latency không thoái hóa. **Risk/rollback:** cache nhỏ tăng OCR, FPS thấp bỏ lỡ chữ; giữ ngưỡng cũ.

### Phase 8 — CPU Model Evaluation (P3, sau 1–3)

**Hypothesis:** INT8/QDQ hoặc PP-OCR mobile khác có thể giảm inference; đổi model không sửa backlog. So sánh FP32 hiện tại, INT8/QDQ và model phù hợp ngôn ngữ bằng cùng dataset/preprocessing, package/license; CPU trước, DirectML/FP16/CUDA tùy chọn. **File dự kiến:** model manifests/config, benchmark, detector/recognizer nếu format thay. **Test:** CER, detection recall, reading order EN/CJK, p50/p95, warm/cold, RAM/package. **Acceptance:** chỉ đổi model khi lợi ích end-to-end rõ và accuracy qua ngưỡng Phase 1; 20% là goal, không phải kết quả. **Risk/rollback:** regression ngôn ngữ/unsupported ops; giữ FP32 hiện tại.

## 5. Pull Request Plan

Mỗi PR phải mô tả problem, source evidence, scope/dependencies, solution, files, test, benchmark, acceptance, regression risk và rollback như phase tương ứng. Không tạo PR chỉ để đổi tên hoặc sắp xếp file. Tên dưới đây là gợi ý, không tạo PR trong tác vụ này.

| Thứ tự | PR gợi ý | Phụ thuộc | Trọng tâm |
| --- | --- | --- | --- |
| 0 | `fix/ocr-cache-thread-safety` | Không | P0 cache race, context và lifecycle |
| 1 | `perf/pipeline-tracing-baseline` | Song song 0 | Trace, CPU baseline, accuracy |
| 2 | `perf/ocr-bounded-mailbox` | 0–1 | Queue thật hữu hạn, latest final, region fairness |
| 3 | `perf/cpu-thread-budget` | 1–2 | A/B thread matrix, contention |
| 4 | `perf/translation-qos` | 1–2 | Timeout/429/fallback, interactive latency |
| 5 | `perf/hover-band-experiment` | 1–3 | Accuracy/latency A/B, feature flag |
| 6 | `perf/tile-and-plate` | 1–2 | Dedup, reading order, plate/RAM |
| 7 | `perf/wgc-memory-capture` | 1 | Fallback diagnostics, RAM dài hạn |
| 8 | `perf/cpu-model-evaluation` | 1–3 | FP32/INT8, accuracy gate |

Nếu không đạt acceptance, ghi kết quả âm và rollback/dừng, không tuyên bố tối ưu thành công.

## 6. Chưa thể kết luận từ source

Chưa có baseline đáng tin cho p50/p95 end-to-end trên i5-12500H, queue wait thực, Qt queued calls, RAM dài hạn, cache hit rate, contention CTranslate2, tần suất 429, WGC/GDI latency và lợi ích adaptive band/tile refactor/INT8. Phase 1 hoặc thí nghiệm tương ứng phải trả lời; không gán chúng là lỗi đã chứng minh. Cần kiểm chứng Windows thực tế cho WGC helper/deployment, DPI/resize, optional DirectML, game load và Qt lifecycle.

## 7. Revision Summary

| Nội dung plan cũ | Xử lý | Lý do |
| --- | --- | --- |
| Mục tiêu phản hồi, accuracy, RAM | Giữ | Vẫn đúng, chuyển thành CPU-first với baseline |
| Instrumentation, benchmark, latest-wins | Giữ và mở rộng | Trace request, tile/plate/queue và region scheduling |
| Parallel CPU recognition là tối ưu sẵn có | Sửa | Cache write đa luồng là P0 correctness |
| Stale hover chỉ do token | Sửa | Qt queued calls không bị chặn bởi map token |
| Region/plate, scroll/cache | Bổ sung | Context geometry, worker plate, scheduler khác README |
| Hover band nhỏ hơn, tiled detect-once, INT8 nhanh hơn | Hoãn thành thí nghiệm | Thiếu A/B, có rủi ro accuracy |
| Shared `Ort::Env` như lời giải global thread pool | Loại bỏ kết luận | Không bảo đảm global pool; phải đo thread thật |
| WGC helper chắc chắn deploy lỗi, GDI làm OCR chậm | Loại bỏ kết luận | CMake đã có dependency/output path; cần benchmark capture độc lập |
| GPU rời/FP16/CUDA, mục tiêu p95 cố định | Hạ ưu tiên | Máy đích không có GPU rời; số cũ chỉ là goals |
| Translation/cache/fallback cần xây mới | Loại bỏ | Đã tồn tại; trọng tâm QoS, 429 và stale result |
