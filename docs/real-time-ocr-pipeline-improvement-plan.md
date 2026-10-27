# Kế hoạch cải tiến OCR và dịch thời gian thực cho EZ Translator

**Ngày lập:** 27/10/2026  
**Phạm vi:** ứng dụng Windows hiện tại, ưu tiên máy i5-11500H dùng CPU; vẫn giữ khả năng chạy GPU trên máy phù hợp.  
**Trạng thái:** Phase 1 đã triển khai và build thành công; một phần theo dõi cuộn được giữ từ working tree hiện tại. Đánh giá trạng thái và phần còn thiếu được ghi chung tại `docs/real-time-ocr-optimization-report.md`. Đây là kế hoạch tiếp nối `docs/ocr-performance-optimization-plan.md`, tập trung vào các nút thắt còn lại sau các đợt tối ưu trước.

## 1. Mục tiêu trải nghiệm

1. **Hội thoại game:** chỉ OCR khi chữ trong vùng thoại xuất hiện hoặc đổi; một câu thoại chỉ dịch một lần. Nền game chuyển động không làm mất bản dịch đang hiển thị. Chữ mới phải xuất hiện đủ sớm để theo kịp nhịp đọc, thay vì cố OCR mọi khung hình của game.
2. **Trang web/PDF:** bản dịch đã xác nhận di chuyển cùng chữ gốc khi cuộn. Khi vị trí được theo dõi chắc chắn, chỉ OCR dải mới đi vào vùng nhìn thấy. Khi nhảy trang hoặc mất dấu, không giữ bản dịch cũ lên nội dung khác.
3. **Dịch bằng con trỏ:** không chạy OCR vùng/Overlay ở nền; việc rê chuột nhiều lần không tạo hàng đợi tăng vô hạn hoặc giành hết CPU của game.
4. **CPU:** giảm lượng công việc cần làm trước khi tăng số luồng hoặc thay model. Chất lượng OCR, bản dịch, bố cục và RAM phải được kiểm tra cùng với tốc độ.

“Thời gian thực” ở đây là phản hồi theo **sự kiện chữ thay đổi**, không đồng nghĩa phải dịch đủ 30 khung hình/giây. Bản dịch có thể tồn tại qua nhiều khung hình nếu nội dung chữ không đổi.

## 2. Hiện trạng đã kiểm chứng

### 2.1 Đường xử lý hiện tại

`WGC/GDI → CapturedFrame → RegionManager → dHash/OcrScheduler → OcrRegionMailbox → OcrWorker → detector → recognizer → TranslationWorker/CT2 → TextOverlay`

- `src/app/AppController.cpp::onCaptureFrame` nhận frame, trích vùng, so hash, theo dõi cuộn, kiểm tra cache và lập lịch OCR. `src/app/AppController.cpp::onHoverTick` điều khiển OCR con trỏ riêng.
- `src/region/RegionManager.cpp::extractRegions` cắt vùng chọn từ frame. Nếu không có ROI trong chế độ Overlay, toàn cửa sổ trở thành một vùng.
- `src/detection/ImageHash.h` thu toàn ảnh còn 9×8 pixel để tính dHash 64 bit; `src/ocr/OcrScheduler.*` dùng khoảng cách Hamming của hash này. Mặc định hiện tại là `settleMs=15`, `minRunIntervalMs=60`.
- `src/ocr/OcrRecognizer.cpp` chia dòng dài thành nhiều đoạn. Log CPU hiện tại ghi `recMaxWidth=480`, `recWorkers=4` cho vùng và `recWorkers=1` cho hover.
- `src/scroll/ScrollOcrReuse.cpp` chỉ OCR dải mới nếu khớp được chuyển động; nếu không sẽ trở về OCR toàn vùng.
- `src/translation/TranslationWorker.cpp` có cache/QoS; phiên log mới dùng CT2 offline Anh → Việt. `src/overlay/TextOverlay.cpp` hiển thị kết quả theo box và plate che chữ gốc.

### 2.2 Bằng chứng trong log mới nhất

Nguồn: `build/ez_debug.log`, phiên bắt đầu lúc **22:14:45 ngày 26/10/2026**. Backend chụp là WGC, kích thước 2867×1691, cấu hình 30 fps. OCR dùng CPU. Log ghi nhận:

| Quan sát | Số liệu mẫu | Ý nghĩa |
| --- | --- | --- |
| OCR vùng | 20 yêu cầu; 19 `full`, 1 `scroll-band` | Nhánh OCR dải mới ít được dùng trong phiên này. |
| Yêu cầu bị hủy | 13/20; một số đã chạy recognition hơn 1–3 giây | CPU đã làm việc nhưng không tạo kết quả hiển thị. |
| OCR toàn vùng, lượt đầu | 2.712 ms, detector 409 ms, recognizer 2.268 ms, 16 box nhưng 38 `recCalls` | Recognition và số lần gọi model là nút thắt chính. |
| OCR toàn vùng, lượt nặng | 4.987 ms, detector 488 ms, recognizer 4.410 ms, 22 box nhưng 61 `recCalls` | Vùng rộng/dòng dài làm lượng inference tăng mạnh. |
| OCR dải mới | 897 ms, detector 58 ms, recognizer 816 ms, 12 `recCalls` | Đường incremental có lợi khi khớp được, nhưng vẫn cần giảm đoạn nhận diện. |
| Hiện bản dịch hoàn chỉnh | Có lượt 1.463 ms; cũng có lượt 4.300 và 6.940 ms | Trải nghiệm dao động lớn; không chỉ do gọi API dịch. |

Các số trên là **mẫu quan sát**, không phải p50/p95 hay bảo đảm cho mọi nội dung. `recMs` là thời gian theo đồng hồ của bước recognition, không được cộng máy móc với thời gian dịch chạy song song. Log và working tree có thể không cùng một bản build: hiện có thay đổi chưa commit trong `AppController.cpp/.h`, `TextOverlay.cpp`, `test_scroll_ocr_reuse.cpp`. Khi bắt đầu triển khai phải lưu commit và build hash để so sánh đúng phiên bản; không ghi đè các thay đổi đó.

### 2.3 Nguyên nhân theo mức tác động

**P0 — Công việc thừa do chế độ xử lý.** Trong `onCaptureFrame`, nếu còn ROI thì nhánh OCR vùng vẫn tiếp tục sau khi chuyển sang chế độ HoverTranslate. Log cho thấy OCR vùng và hover hoạt động cùng lúc. Cùng với CT2, các worker này tranh CPU trên máy không có GPU rời.

**P0 — Vùng chọn thay đổi làm hủy OCR đắt tiền.** `OcrScheduler` xem thay đổi của dHash toàn ROI là nội dung mới; chuyển động nền game hoặc cuộn trang có thể thay đổi hash dù chữ cần dịch chưa đổi. `AppController` vô hiệu hóa token OCR đang chạy và có thể xóa box Overlay khi không theo dõi được chuyển động. ONNX Run đang diễn ra không thể thu hồi chi phí đã dùng.

**P0 — Hai bộ theo dõi cuộn không cùng mốc ảnh.** Overlay so các frame liên tiếp, có thử dải cột chứa chữ; `ScrollOcrReuse` so với ảnh OCR được chấp nhận trước đó, dùng ngưỡng điểm khớp và giới hạn dịch chuyển khác. Vì vậy Overlay có thể giữ/di chuyển chữ trong khi worker OCR báo `low-score` hoặc `page-jump` rồi quét toàn vùng. Cần xác nhận chuyển động từ cùng một snapshot thay vì hạ ngưỡng một cách đơn lẻ.

**P1 — Quá nhiều lần gọi recognizer.** Với `recMaxWidth=480`, dòng dài bị tách thành nhiều đoạn; mỗi ảnh đầy chữ có hàng chục `recCalls`, cache crop thường không trúng sau dịch chuyển/đổi khung. Tăng worker chỉ chia tải trên cùng CPU và có thể làm game chậm hơn.

**P1 — Dịch và dựng Overlay.** CT2 offline không phải nguồn trễ duy nhất: lượt nặng ghi 1,5 giây cho dịch bên cạnh 5 giây OCR. Overlay xóa box khi mất dấu gây nhấp nháy; giữ mù quáng lại gây chữ Việt đè sai lên chữ Anh. Cần có quy tắc giữ/xóa theo độ tin cậy vị trí.

**P2 — Chụp và tiền xử lý.** WGC đã hoạt động; frame lớn và cắt ROI mỗi khung có chi phí bộ nhớ/băng thông. Cần đo riêng trước khi đổi capture. `tiles=1` trong lượt nặng nên chia tile không phải nguyên nhân của phiên này.

## 3. Nguyên tắc thiết kế chung

### 3.1 Tách ba chế độ xử lý

- `Hover`: chỉ OCR một vùng quanh con trỏ theo yêu cầu; không chạy OCR ROI của Overlay.
- `GameDialog`: ROI cố định, nhỏ, có thể chứa nền động; dựa vào **chữ đã đọc** và trạng thái câu thoại để quyết định dịch.
- `DocumentScroll`: ROI lớn, quản lý vị trí theo cuộn và chỉ OCR dải mới khi chuyển động đã xác nhận.

Giao diện có thể tự chọn preset hoặc để người dùng chọn. `GameDialog` là chế độ kỹ thuật tổng quát cho phụ đề/hội thoại ở vị trí tương đối cố định, không phụ thuộc một game cụ thể. Vẫn giữ đường OCR toàn vùng như phương án dự phòng cho ảnh mới hoặc mất dấu.

### 3.2 Một request phải có ngữ cảnh bất biến

Mỗi yêu cầu OCR/dịch mang `sessionGeneration`, `mode`, `windowId`, `regionId`, `frameSequence`, `captureTime`, `ROI`, `sourceHash`, `requestToken` và vị trí theo tọa độ frame. Chỉ trình bày kết quả khi các trường liên quan còn hợp lệ. Kết quả dịch có thể dùng lại theo **nội dung câu**, nhưng box/vị trí chỉ dùng khi còn khớp nguồn. Cấm lấy geometry mới gắn lên OCR từ frame cũ.

### 3.3 Một vị trí, một nguồn chữ

Overlay lưu `sourceText`, `translation`, `sourceRect`, `frameSequence`, `trackId`, `confidence` và trạng thái `verified/translated/uncertain`. Chỉ che chữ gốc trong box đã xác nhận. Nếu vị trí chưa chắc chắn, ưu tiên hiện chữ gốc thay vì đặt bản dịch cũ lên vị trí sai; khi cuộn đã khớp thì dịch chuyển box ngay trên UI thread, không chờ OCR.

## 4. Lộ trình triển khai tuần tự

Mỗi phase chỉ coi là hoàn thành khi đã build, chạy các phép kiểm tra liên quan, so với baseline cùng workload, và ghi báo cáo `docs/real-time-performance-reports/phase-N.md`. Báo cáo cần chứa commit/build hash, cấu hình máy, mẫu ảnh hoặc video, số đo trước/sau, lỗi còn lại và quyết định giữ/rollback. Không dùng số mục tiêu bên dưới như kết quả đã đạt.

### Phase 0 — Baseline có thể tái lập và đo đúng đường đi

**Thực hiện**

1. Ghi commit/build hash, provider OCR, model và engine dịch, số thread, độ phân giải frame/ROI, chế độ UI và môi trường CPU/GPU vào đầu log mỗi phiên.
2. Gắn cùng `requestId` xuyên suốt `capture → changed/settled → queued → OCR start → det → rec → translate → overlay presented`. Phân biệt frame mới, cache hit, partial presentation và bản dịch hoàn chỉnh; không gộp lượt bị hủy với lượt thành công.
3. Ghi thêm `cancelReason`, `recCalls`, `segments/line`, `boxCount`, `scrollFallbackReason`, `scrollScore`, `overlayKept`, `regionOcrWhileHover`, queue depth và thời gian chờ. Log chỉ metadata/timing; không ghi nguyên văn nội dung riêng tư theo mặc định.
4. Thu ít nhất ba workload lặp lại được: hội thoại game có nền động/chữ xuất hiện dần; PDF cuộn chậm, cuộn nhanh, nhảy trang; hover trên trang nhiều chữ. Cố định ROI và kịch bản để so sánh các build.

**Mã dự kiến:** `src/app/AppController.*`, `src/ocr/OcrWorker.*`, `src/ocr/OcrRecognizer.cpp`, `src/translation/TranslationWorker.cpp`, công cụ trong `tools/`.  
**Đầu ra:** bảng p50/p95 thời gian từ chữ mới đến lúc hiện, theo từng chế độ; số lần OCR/model trên một câu; CPU/RAM trong 10 phút; lỗi overlay bị mất/đè sai.  
**Điều kiện hoàn thành:** có dữ liệu thô và lệnh/kịch bản chạy lại được. Đây là mốc so sánh cho mọi phase sau.

### Phase 1 — Loại bỏ OCR sai chế độ và đặt ngân sách CPU chung

**Thực hiện**

1. Trong `AppController::onCaptureFrame`, sau khi cập nhật ảnh mới nhất cho hover/preview, **dừng trước đường ROI OCR** nếu chế độ là HoverTranslate. Khi đổi chế độ, bỏ pending ROI và vô hiệu hóa result cũ theo generation; không xóa cache câu dịch.
2. Trong `onHoverTick`, chỉ chạy hover khi đúng chế độ và cửa sổ đích còn hợp lệ. Bảo đảm mỗi đường có tối đa một lượt đang chạy và một lượt chờ mới nhất; ưu tiên kết quả final hơn prefetch cũ.
3. Đặt ngân sách thực thi CPU tổng cho region OCR, hover OCR và CT2 thay vì xem từng worker độc lập. Bắt đầu bằng policy bảo thủ cho CPU, đo game FPS/UI responsiveness rồi mới điều chỉnh. Không đổi policy GPU nếu chưa có benchmark.
4. Đảm bảo kết quả về sau khi đổi mode/window không được vẽ lên màn hình hiện tại.

**Mã dự kiến:** `src/app/AppController.cpp/.h`, `src/ocr/OcrRegionMailbox.*`, `src/ocr/OcrWorker.*`, `src/ocr/OcrThreadBudget.*`, `src/translation/TranslationThreadBudget.*`.  
**Kiểm tra:** đổi mode liên tục khi OCR cố tình chậm; hover nhiều lần; đóng/mở app; chạy game cùng lúc; test mailbox và token stale.  
**Điều kiện hoàn thành:** `regionOcrWhileHover=0` sau khi chuyển chế độ; không còn vùng OCR mới ở chế độ hover; không tăng RAM theo số thao tác chuột; p95 hover không xấu đi so với baseline. Phần OCR đang chạy trước khi chuyển mode có thể phải hoàn tất ở ONNX, nhưng không được bắt đầu lượt vùng tiếp theo hoặc hiện kết quả cũ.

### Phase 2 — Đường nhanh cho hội thoại game

**Thực hiện**

1. Thêm preset `GameDialog` sử dụng ROI nhỏ, khóa theo cửa sổ game, lưu độc lập với ROI tài liệu. Nếu chọn cả cửa sổ, hướng dẫn chọn vùng thoại; không tự quét toàn màn hình 30 fps.
2. Từ ROI, tính tín hiệu thay đổi tập trung vào nét chữ/các dòng chữ hiện có; loại bớt nền động bằng mask hoặc so sánh các dải chứa text. Dùng khung hình nhẹ để sàng lọc, chỉ OCR khi khả năng chữ đã đổi đủ cao. Nếu chưa có box chữ, OCR phát hiện lần đầu rồi tạo mask.
3. Sau OCR, chuẩn hóa câu (`whitespace`, lỗi dấu câu nhỏ) và so với câu gần nhất. Nếu text giống hoặc biến động nhỏ, giữ nguyên translation và box; nếu text đang dài dần kiểu typewriter, giữ bản dịch ổn định trước đó rồi chờ điểm dừng ngắn hoặc dấu kết câu. Giới hạn thời gian chờ tối đa để câu không có dấu kết vẫn được dịch.
4. Cache theo `(ngôn ngữ nguồn, ngôn ngữ đích, engine, câu chuẩn hóa, ngữ cảnh nếu có)`. Không gắn vị trí cũ vào câu cache khi ROI thay đổi. Hàng đợi lấy bản mới nhất cho cùng hộp thoại, nhưng không hủy model đang chạy chỉ vì **nền** đổi.
5. Trường hợp game thay cảnh, thay ROI, không tìm được chữ hoặc confidence thấp: xác minh ảnh hiện tại và xóa Overlay theo quy tắc rõ ràng; không giữ câu thoại của cảnh trước.

**Mã dự kiến:** thành phần phát hiện thay đổi riêng ở `src/detection/`, `src/app/AppController.*`, `src/ocr/OcrScheduler.*`, `src/ocr/OcrWorker.*`, `src/translation/TranslationCache.*`, `src/overlay/TextOverlay.*`, cài đặt chế độ ở `src/ui/`.  
**Kiểm tra:** game nền động nhưng thoại đứng yên; chữ hiện từng ký tự; thoại mới ở cùng vị trí; thoại biến mất; menu nhiều dòng; cửa sổ game mất focus.  
**Điều kiện hoàn thành:** nền động một mình không sinh OCR/dịch mới hoặc làm mất translation; một câu ổn định có tối đa một lượt dịch mới; không che chữ khác khi đổi cảnh. Mục tiêu ứng viên cho ROI 2–4 dòng trên i5-11500H: p95 từ lúc câu ổn định đến khi hiện ≤1,5 giây, nhưng chỉ chốt sau baseline và đo thực.

### Phase 3 — Hợp nhất theo dõi cuộn và OCR dải mới

**Thực hiện**

1. Tạo một `ScrollObservation` chung cho Overlay và worker OCR gồm frame sequence, ảnh tham chiếu, ROI, dịch chuyển `(dx, dy)`, confidence, vùng trùng nhau và lý do không khớp. So frame liên tiếp trên vùng chứa chữ; không để hai tracker tự quyết theo hai mốc ảnh khác nhau.
2. Khi dịch chuyển đã xác nhận, dịch chuyển ngay các box/các plate tương ứng và giữ bản dịch trên phần ảnh còn trùng. Cắt box ở mép; không kéo một box đã ra khỏi màn hình về vị trí khác.
3. Dồn phần ảnh mới lộ ra ở mép vào một hoặc vài dải; chỉ OCR dải này sau khi ảnh ổn định. Giữ translation của các track cũ; chỉ dịch câu mới. OCR xác minh định kỳ khi nghi ngờ box bị lệch.
4. Với cuộn nhanh, tích lũy dịch chuyển qua các frame và tránh OCR mỗi bước. Sau khi dừng, OCR phần mới thấy. Với nhảy trang/zoom/thay cửa sổ, reset track và OCR đầy đủ một lần sau ổn định.
5. Nếu confidence thấp, chuyển box sang `uncertain`: không che chữ tại tọa độ không được xác minh. Tránh hai lỗi đối nghịch: nhấp nháy khi cuộn thật và chữ dịch cũ đè chữ gốc khác khi khớp nhầm.

**Mã dự kiến:** `src/scroll/ScrollEstimator.*`, `ScrollTracker.*`, `ScrollOcrReuse.*`, `src/app/AppController.*`, `src/overlay/TextOverlay.*`, `tests/test_scroll_ocr_reuse.cpp`, `tests/test_overlay_geometry.cpp`.  
**Kiểm tra:** cuộn chậm/nhanh hai chiều, cuộn xa, trang nhiều khoảng trắng, hai cột, zoom, nhảy trang, chuyển tab; video kiểm tra box tại từng frame.  
**Điều kiện hoàn thành:** số lần OCR toàn vùng trên một kịch bản cuộn giảm rõ so với baseline; bản dịch đã khớp không biến mất khi cuộn liên tục; **0 trường hợp tái hiện được** bản dịch cũ đè lên đoạn nguồn khác trong tập kiểm thử; `scroll-band` tăng nhưng không nhờ nới ngưỡng gây ghép nhầm.

### Phase 4 — Giảm chi phí recognition trên CPU

**Thực hiện**

1. Đo `recCalls/box` theo chiều rộng dòng và loại nội dung. Kiểm tra detector có tách một dòng thành nhiều box không; gộp box theo dòng/cột **trước** recognition khi hình học cho phép, giữ thứ tự đọc.
2. A/B các mức `recMaxWidth` thay vì cố định 480, overlap giữa đoạn, số `recWorkers`, CPU batch nhỏ và giới hạn thread chung. Mỗi lựa chọn phải đo tổng thời gian đến Overlay và độ chính xác; nhanh hơn từng ONNX Run chưa chắc nhanh hơn cả pipeline.
3. Thử cache crop bền hơn với dịch chuyển nhỏ sau khi chuẩn hóa ảnh, nhưng kiểm tra nguy cơ hai từ khác nhau trùng khóa. Cache bản dịch theo text tách khỏi cache vị trí.
4. Chạy cùng bộ ảnh trên model OCR khác, ví dụ OCR hệ thống Windows/OneOCR nếu có thể tích hợp, hoặc model ONNX nhỏ hơn. So CER/WER, recall, thời gian cold/warm, RAM và chi phí đóng gói. Chỉ đổi mặc định nếu có lợi tổng thể trên cả game và tài liệu, hoặc tách preset theo workload.

**Mã dự kiến:** `src/ocr/OcrDetector.*`, `OcrRecognizer.*`, `OcrEngine.*`, `OcrTypes.h`, `tools/ocr_bench.cpp`, `tools/ocr_model_eval.cpp`.  
**Kiểm tra:** chữ game nhỏ/viền sáng, văn bản PDF hai cột, dòng dài, tiếng Anh có code/ký hiệu, chữ sát mép, CPU và GPU nếu có.  
**Điều kiện hoàn thành:** giảm p95 recognition và `recCalls` trên tập nặng; CER/WER không vượt ngưỡng suy giảm đã chốt từ baseline; không tăng RAM/giật game ngoài mức chấp nhận. Không hứa mức tăng tốc trước A/B.

### Phase 5 — Dịch, Overlay và xác nhận toàn hệ thống

**Thực hiện**

1. Giữ CT2 offline làm baseline; đo số đoạn gửi dịch, cache hit, queue wait và `translate_batch`. Gom các câu mới vừa đủ để giảm overhead nhưng không buộc một câu game ngắn chờ cả trang tài liệu. Đặt ưu tiên game/hover so với dịch tài liệu dài.
2. Bảo đảm Overlay chỉ che đúng vùng nguồn đã xác nhận; cỡ chữ Việt gần nguồn nhưng không tràn sang khối khác. Khi chưa có bản dịch, giữ nguyên chữ gốc; khi có bản dịch mới, cập nhật theo trackId để tránh text Việt/Anh lẫn nhau hoặc bản cũ đè bản mới.
3. Chạy test end-to-end tối thiểu 10 phút mỗi chế độ; đo RAM, CPU, game FPS, p50/p95 từ chữ mới đến hiển thị, tỉ lệ OCR bị hủy, tỉ lệ `scroll-band`, số lần Overlay biến mất và lỗi hình học. So cùng dữ liệu đầu vào với baseline Phase 0.
4. Dọn lại tài liệu/cài đặt: giải thích khi nào chọn `GameDialog`, `DocumentScroll`, `Hover`, cách chọn ROI và giới hạn hiệu năng CPU. Nếu preset gây lỗi ở workload khác, giữ khả năng trở về policy cũ trong cài đặt khi phát hành.

**Mã dự kiến:** `src/translation/TranslationWorker.*`, `DocumentTranslator.*`, `tools/ct2_server.py`, `src/overlay/*`, `src/app/AppController.*`, `src/ui/*`.  
**Điều kiện hoàn thành:** có bảng trước/sau và video kiểm tra trực quan; không còn lỗi stale overlay đã biết; tốc độ cải thiện trên cả ROI thoại và PDF cuộn, không chỉ ở benchmark micro; RAM ổn định trong phiên dài.

## 5. Cách đánh giá và thứ tự quyết định

| Chỉ số | Định nghĩa | Vì sao cần |
| --- | --- | --- |
| `textStableToPresentedMs` | Từ lúc câu nguồn ổn định đến lúc bản dịch hoàn chỉnh hiện | Đúng cảm nhận người dùng game; không tính frame nền đổi là câu mới. |
| `captureChangeToPresentedMs` | Từ lúc ảnh vùng đổi thật đến lúc hiện kết quả | Theo dõi tài liệu/cuộn. |
| `detMs`, `recMs`, `recCalls`, `recCached` | Chi phí phát hiện/nhận diện và số lượt model/cache | Xác định điểm tốn CPU. |
| `cancelledAfterInferenceMs` | Thời gian model đã dùng cho request rồi bị hủy | Đo công việc lãng phí. |
| `fullOcrPerNewSentence`, `scrollBandRatio` | Số OCR toàn vùng trên một câu mới và tỷ lệ OCR dải | Xác nhận kiến trúc event-driven/incremental thực sự được dùng. |
| `overlayGapFrames`, `wrongAnchorCount` | Số frame mất bản dịch khi cuộn và số lần dịch cũ đè sai | Tốc độ phải đi cùng độ đúng hình học. |
| CPU, working set, game FPS | Ghi trong cùng kịch bản trên i5-11500H | Tránh tăng tốc app bằng cách làm game giật. |

Đo ít nhất 20 lượt **chữ mới** cho game và nhiều chu kỳ cuộn lặp lại cho PDF; báo p50/p95 kèm số mẫu. Chạy cold và warm riêng, cache hit/miss riêng. Không so phiên WGC với GDI hoặc ROI nhỏ với toàn màn hình như thể cùng workload. Nên quay màn hình có timestamp để đối chiếu log với thời điểm người dùng nhìn thấy chữ.

**Quy tắc ưu tiên:** nếu Phase 1 đã giảm tranh CPU nhưng Phase 2 chưa đạt tốc độ, tập trung vào số `recCalls` và độ rộng ROI; nếu Phase 3 vẫn fallback nhiều, sửa motion reference/mask trước khi hạ ngưỡng score; nếu OCR đã nhanh mà tổng thời gian vẫn chậm, mới ưu tiên batching/cache CT2 và dựng Overlay. Chỉ lựa chọn model khác dựa trên A/B có độ chính xác và RAM.

## 6. Bài học tham khảo từ ứng dụng khác

- [RSTGameTranslation — FrameChangeDetector](https://github.com/thanhkeke97/RSTGameTranslation/blob/main/src/FrameChangeDetector.cs): bỏ OCR khi frame chưa đổi so với ảnh đã OCR; [Logic](https://github.com/thanhkeke97/RSTGameTranslation/blob/main/src/Logic.cs) theo dõi chữ đang hiện dần, chờ ổn định, so text và cache dịch. Phù hợp để tham khảo cho `GameDialog`, không phải bằng chứng rằng OneOCR luôn nhanh hơn model hiện tại.
- [SubLens](https://github.com/bnsware/SubLens): chọn vùng hội thoại, quét theo chu kỳ, bỏ qua OCR jitter và cache câu dịch. Nguyên tắc có ích là thu hẹp công việc vào nơi chữ thực sự xuất hiện.
- [RenpyLens](https://github.com/liuyuan-wen/RenpyLens): với engine được hỗ trợ, lấy trực tiếp text của game để tránh OCR. Có thể thành plugin tương lai, nhưng không thay thế đường OCR tổng quát của EZ Translator.

## 7. Việc chưa nằm trong phạm vi đợt này

- Không cam kết dịch mọi frame của video game ở 30 fps; mục tiêu là bắt kịp **câu thoại mới**.
- Không mặc định thay model, bật GPU hay tăng số worker trước khi có so sánh trên cùng dữ liệu.
- Không hy sinh độ đúng vị trí/đọc chữ để đạt một con số thời gian. Khi track không chắc chắn, Overlay phải tránh che sai.

**Bước triển khai đầu tiên:** hoàn thành Phase 0 để khóa baseline, rồi Phase 1 để ngừng OCR vùng khi đang dịch con trỏ. Đây là lỗi kiến trúc có bằng chứng rõ nhất và có tiêu chí kiểm chứng nhanh nhất.
