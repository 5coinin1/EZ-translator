# Báo cáo tối ưu OCR và pipeline dịch thời gian thực

## 1. Phạm vi

Tài liệu này ghi lại các phần đã triển khai trong EZ Translator cho pipeline:

~~~text
Capture -> phát hiện thay đổi -> OCR detection -> OCR recognition -> dịch -> overlay
~~~

Mục tiêu chính là giảm thời gian từ lúc nội dung trong game thay đổi đến lúc bản dịch xuất hiện, đồng thời giữ độ ổn định khi người dùng cuộn trang hoặc khi máy không có GPU rời.

Trạng thái hiện tại:

- OCR chạy bằng PaddleOCR ONNX với model detection và recognition tách riêng.
- Máy có GPU rời đủ điều kiện có thể dùng DirectML hoặc CUDA.
- Máy không có GPU rời tự chuyển OCR sang CPU.
- Dịch tiếng Anh sang tiếng Việt đang chạy bằng model CTranslate2 offline qua tiến trình Python phụ.
- Khi cuộn được nhận diện đúng, app dùng scroll-band và chỉ OCR dải ảnh mới xuất hiện.
- Bản build chính là build/EZTranslator.exe.

## 2. Các phần đã hoàn thành

### 2.1. Điều phối luồng OCR và dịch

Đã tách thời gian xử lý thành các luồng riêng cho capture, OCR và dịch. OCR không chạy trực tiếp trên luồng giao diện, vì vậy việc inference hoặc dịch chậm không khóa cửa sổ chính.

Đã bổ sung ngân sách thread theo phần cứng:

- GPU: dùng một worker inference trên session GPU để tránh nhiều lượt Run tranh chấp cùng session.
- CPU: dùng intra-op=1, recognition chạy song song bằng nhiều worker độc lập.
- Hover: dùng ngân sách nhỏ hơn vì thường chỉ xử lý một dòng chữ.

Tác dụng là giữ capture và giao diện phản hồi được trong lúc OCR hoặc dịch đang chạy.

### 2.2. Tự chọn GPU hoặc CPU

App kiểm tra GPU rời và điều kiện VRAM trước khi chọn execution provider. Nếu không đạt điều kiện, log sẽ ghi:

~~~text
[OCR] Không phát hiện GPU rời đủ VRAM; chuyển OCR sang CPU.
~~~

Cách này tránh việc cố chạy DirectML trên thiết bị onboard gây chiếm RAM hệ thống, tăng bộ nhớ dần hoặc inference không ổn định.

Phần tối ưu scroll, cache, hủy request và dịch offline áp dụng cho cả GPU lẫn CPU. Các cấu hình worker và chiều rộng recognition được chọn khác nhau theo provider.

### 2.3. Giảm chi phí recognition trên dòng dài

Model recognition nhận ảnh dòng chữ với chiều cao chuẩn hóa 48 px. Dòng quá dài được chia thành các đoạn có chồng lấn, sau đó ghép lại bằng phần trùng nhau.

Trên CPU, recMaxWidth mặc định là 480 px. Trên GPU, giá trị mặc định là 1920 px để giảm số lần dispatch lên GPU. Có thể A/B bằng biến môi trường:

~~~text
EZ_OCR_REC_MAX_WIDTH=480
~~~

Tác dụng:

- CPU không phải đưa một dòng rộng 1000–1900 px vào một lượt inference nặng.
- Các đoạn được chạy song song trên CPU.
- GPU vẫn có thể xử lý đoạn rộng để tận dụng batch và giảm số lượt gọi model.

### 2.4. Cache recognition theo nội dung dòng

Recognition có cache LRU tối đa 4096 đoạn dòng. Cache dùng hash ảnh đã chuẩn hóa của đoạn chữ, nên dòng không đổi có thể dùng lại kết quả nhận diện thay vì gọi model lần nữa.

Cache này khác với cache dịch CTranslate2:

- Cache OCR: tối đa 4096 đoạn recognition.
- Cache dịch offline: mặc định tối đa 2000 mục trong sidecar CTranslate2.

Giới hạn cache đủ lớn cho một phiên đọc thông thường. Hiệu quả thực tế phụ thuộc vào việc ảnh crop có giữ ổn định hay không; thay đổi khử răng cưa, tọa độ crop hoặc nội dung động sẽ tạo hash mới.

### 2.5. Hủy và gộp request OCR

Khi capture tạo nhiều request trong lúc người dùng đang cuộn:

- Request cũ được đánh dấu hủy.
- Request mới nhất được giữ lại trong mailbox.
- Các request trùng vùng được coalesced.
- Kết quả của request đã lỗi thời không được cập nhật overlay.

Điều này ngăn hàng đợi OCR chạy hết các frame cũ. Một lượt ONNX đã bắt đầu vẫn có thể dùng một phần thời gian trước khi kiểm tra hủy, nhưng kết quả cũ không tiếp tục đi vào pipeline dịch và overlay.

### 2.6. Phát hiện thay đổi và giảm OCR khi khung hình đứng yên

App dùng profile ảnh nhẹ để kiểm tra mức thay đổi giữa các frame. Khi vùng đứng yên, app bỏ qua frame và không gọi OCR mới. Khi phát hiện chuyển động hoặc nội dung mới, app mới tạo request OCR.

Phần này giảm CPU khi game không thay đổi nội dung, đồng thời giảm số lần dịch lại cùng một đoạn.

### 2.7. Tối ưu detection trên ảnh lớn

Detection resize ảnh về giới hạn cạnh dài trước khi đưa vào model. Với cửa sổ lớn, pipeline có thể chia ảnh thành các tile có overlap để giữ khả năng phát hiện chữ nhỏ ở nhiều khu vực.

Đã có đường detect-once:

1. Detection trên các tile.
2. Ghép và loại box trùng ở vùng overlap.
3. Recognition trên ảnh gốc sau khi đã hợp nhất box.

Cách này tránh recognition cùng một dòng nhiều lần do dòng nằm trên ranh giới tile.

### 2.8. OCR tái sử dụng khi cuộn

ScrollOcrReuse lưu profile của frame OCR đầy đủ và các box đã nhận diện. Khi frame mới là một chuyển động cuộn hợp lệ, app:

1. Ước lượng dx và dy bằng profile độ sáng và cạnh chữ.
2. Dịch toàn bộ box cũ theo độ dịch chuyển.
3. Xóa các box gần mép vừa lộ ra.
4. Chỉ OCR dải ảnh mới ở mép đó.
5. Ghép box mới với box cũ.

Ngưỡng điểm cuộn dọc thực tế là 0.92. Các hàng rào an toàn vẫn được giữ:

- Dịch ngang quá lớn: fallback OCR toàn vùng.
- Dịch dọc vượt quá một phần ba chiều cao: coi là chuyển trang.
- Không có dịch chuyển dọc: không dùng lại box.
- Merge không còn text nhìn thấy: fallback để tránh overlay trống.

Đã sửa lỗi bootstrap khiến lần cuộn đầu bị chọn dy=0 do thuật toán ưu tiên vị trí dự đoán bằng 0. Khi tracker chưa có trạng thái, bộ chọn lấy đỉnh tương quan tốt nhất thay vì áp dụng ưu tiên alias theo dự đoán. Khi độ sáng bị nhiễu, profile cạnh chữ được thử thêm để khôi phục dịch chuyển.

### 2.9. Dịch offline CTranslate2

Pipeline dịch hiện tại ưu tiên model local CTranslate2 đúng cặp ngôn ngữ. Log xác nhận:

~~~text
[Translate] engine ct2 sẵn sàng en -> vi
[Translate] cấu hình: engine = "offline" | offline = 1
~~~

Đã điều chỉnh:

- Beam mặc định giảm từ 4 xuống 2.
- Fast path dùng beam 1.
- Giới hạn decoding mặc định giảm xuống 128 token.
- Fast path mặc định giới hạn 96 token mỗi mảnh và batch tối đa 8.
- Timeout phản hồi tăng theo số lượng dòng, tối đa 10 giây, tránh timeout giả khi batch lớn.
- Cache và dedupe trong sidecar để không dịch lại cùng một mảnh.

Mục tiêu của các thay đổi này là giảm thời gian decode và tránh các output lặp giữ CPU quá lâu, trong khi vẫn đủ không gian cho bản dịch hội thoại và text game thông thường.

### 2.10. Không che chữ gốc khi bản dịch rỗng

Overlay chỉ được dựng khi document dịch có text hợp lệ. Nếu engine chưa sẵn sàng hoặc trả về document rỗng, app bỏ qua overlay rỗng thay vì tạo các box che toàn bộ chữ gốc.

Điều này xử lý trường hợp log có complete=false hoặc bản dịch đang chờ nhưng overlay đã được cập nhật sớm.

## 3. Số liệu từ log mới nhất

Phiên chạy mới nhất dùng:

~~~text
Capture: WGC
OCR provider: CPU
detIntra=1, recIntra=1, recWorkers=4
recMaxWidth=480
Translation: CTranslate2 offline en -> vi
~~~

Các lượt OCR toàn vùng vẫn cần nhiều thời gian vì phải nhận diện hàng chục dòng:

| Loại xử lý | OCR | Recognition | Số lượt rec | Kết quả hiển thị dịch |
| --- | ---: | ---: | ---: | ---: |
| Full, roi_full | 2.843 s | 2.381 s | 93 | 4.442 s |
| Full, roi_full | 3.942 s | 3.618 s | 123 | 5.867 s |
| Full, roi_1 | 1.656 s | 1.182 s | 51 | 3.734 s |
| Scroll-band, roi_full, dy=71 | 126 ms | 94 ms | 6 | 1.055 s |
| Scroll-band, roi_1, dy=-184 | 644 ms | 591 ms | 17 | 1.130 s |
| Scroll-band, roi_1, dy=-14 | 104 ms | 80 ms | 2 | 306 ms |

Các dòng log xác nhận cơ chế đã chạy thật:

~~~text
[OCR_SCROLL] region= roi_full dy= 71 score= 0.937985
[OCR_METRIC] ... mode= scroll-band ... totalMs= 126 ... recCalls= 6

[OCR_SCROLL] region= roi_1 dy= -184 score= 1
[OCR_METRIC] ... mode= scroll-band ... totalMs= 644 ... recCalls= 17

[OCR_SCROLL] region= roi_1 dy= -14 score= 1
[OCR_METRIC] ... mode= scroll-band ... totalMs= 104 ... recCalls= 2
~~~

Khi bộ ước lượng phát hiện nhảy trang lớn, app vẫn fallback an toàn:

~~~text
reason=page-jump dy=-530
reason=page-jump dy=-626
~~~

## 4. Phần tối ưu riêng cho CPU

### Cấu hình hiện tại

Máy không có GPU rời sẽ dùng:

~~~text
provider=CPU
detIntra=1
recIntra=1
recWorkers=4
interOp=1
recMaxWidth=480
~~~

Số worker được tính theo số luồng logic nhưng giới hạn tối đa 4 để giữ CPU cho capture, giao diện và dịch. Có thể ghi đè khi cần:

~~~text
EZ_OCR_DET_THREADS=1
EZ_OCR_REC_THREADS=1
EZ_OCR_REC_WORKERS=4
EZ_OCR_INTER_THREADS=1
EZ_OCR_REC_MAX_WIDTH=480
~~~

### CPU được tối ưu như thế nào

Recognition là phần tốn chi phí nhất trên CPU. Trong log, một lượt full có thể có 74–123 recCalls, trong khi detection chỉ chạy một lần. Việc chia dòng ở 480 px làm mỗi lượt recognition nhỏ hơn và cho phép 4 worker xử lý song song.

Scroll-band đem lại lợi ích lớn hơn mọi tinh chỉnh thread khi người dùng đang cuộn. Thay vì nhận diện 50–70 box trên toàn vùng, app chỉ nhận diện box ở dải mới. Log đã giảm từ hàng nghìn mili giây xuống khoảng 104–644 ms cho OCR dải.

Cache recognition giúp các dòng giữ nguyên không chạy lại model. Tuy nhiên cache không thể đảm bảo hit khi ảnh crop thay đổi do animation, antialiasing hoặc vị trí box thay đổi.

Translation chạy bằng CT2 offline với thread budget riêng. Khi OCR và dịch đồng thời, hai phần vẫn có thể cạnh tranh CPU; vì vậy các giới hạn beam, token, batch và worker được đặt thấp để giữ phản hồi ổn định.

### Hiệu quả thực tế trên CPU

Với nội dung mới hoặc chuyển trang, thời gian đầy đủ vẫn khoảng 1,6–5,9 giây tùy số lượng dòng, độ dài text và việc model dự phòng có được gọi hay không. Đây là chi phí của OCR toàn vùng và dịch nhiều paragraph.

Với cuộn liên tục trong cùng trang:

- OCR dải thường khoảng 0,1–0,6 giây.
- Bản dịch hoàn chỉnh thường khoảng 0,3–1,1 giây.
- Số lượt recognition giảm còn khoảng 2–17 lượt trong các mẫu log đã đo.

## 5. Máy có GPU rời được hưởng lợi thế nào

GPU rời vẫn hưởng lợi từ scroll-band, cache, mailbox, hủy request, detection theo tile và tối ưu CTranslate2. Khi fallback về OCR toàn vùng, detection và recognition có thể chạy trên DirectML hoặc CUDA.

GPU dùng cấu hình recognition rộng hơn, thường recMaxWidth=1920, và không fan nhiều worker inference trên cùng session. Điều này giảm số lượt dispatch và tận dụng tốt hơn khả năng xử lý tensor của GPU.

Lợi ích lớn nhất của scroll-band trên GPU là giảm số lần gọi model. Lợi ích trên CPU thường dễ thấy hơn về mặt thời gian vì mỗi lần gọi model CPU đắt hơn, nhưng kiến trúc và cơ chế tiết kiệm được áp dụng cho cả hai provider.

## 6. Kiểm thử và xác nhận

Đã kiểm tra:

- Build EZTranslator.exe thành công.
- Test scroll OCR đạt 10/10.
- Test bootstrap không ưu tiên sai dy=0.
- Test khôi phục bằng profile cạnh chữ.
- Test frame đứng yên không gọi OCR dải.
- Test đổi trang fallback không chạy OCR dải sai.
- Test merge rỗng không làm mất text đang hiển thị.
- Log runtime xác nhận mode=scroll-band trên cả roi_full và roi_1.

Các commit chính:

~~~text
d2f9c32 perf: fix scroll OCR bootstrap and reuse
be173ee perf: integrate scroll reuse and CPU recognition tuning
f83ddfb perf: tune offline translation latency
~~~

## 7. Giới hạn hiện tại và hướng tiếp theo

Full OCR vẫn là điểm tốn thời gian khi nội dung thay đổi hoàn toàn hoặc khi bộ ước lượng xác định đây là chuyển trang. Recognition CPU và dịch nhiều paragraph vẫn có thể tạo độ trễ vài giây.

Các hướng còn lại có giá trị cao nhất:

1. Tách profile cuộn theo vùng nội dung thay vì toàn cửa sổ để giảm ảnh hưởng của header, sidebar và nền đứng yên.
2. Đo độ dịch chuyển trên nhiều dải ngang rồi bỏ phiếu để ổn định khi một phần cửa sổ không cuộn.
3. Làm cache recognition bền hơn với crop lệch nhẹ, nhưng phải giữ kiểm soát tránh lấy nhầm text.
4. Đo riêng thời gian OCR, dịch và dựng overlay trên nhiều loại game để chọn cấu hình CPU/GPU theo workload thực tế.

