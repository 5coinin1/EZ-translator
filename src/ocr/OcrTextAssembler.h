#pragma once

#include "ocr/OcrTypes.h"

#include <QList>
#include <QRect>
#include <QString>

namespace EZTranslator {
namespace OcrTextAssembler {

/** Một dòng đã ghép: rect bao dòng + text của dòng. */
struct LineText
{
    QRect rect;
    QString text;
};

/** Text cả tài liệu + danh sách dòng (giữ rect để đặt bản dịch lại đúng vị trí). */
struct Assembled
{
    QString text;
    QList<LineText> lines;
};

/**
 * @brief Ghép các box đã có text thành văn bản: nhóm theo hàng (tâm y gần nhau),
 *        sắp trái→phải, nối text (chèn khoảng trắng theo khe), phát hiện đoạn theo
 *        khe dòng / thụt đầu dòng.
 *
 * @return text có '\n' giữa các dòng, '\n\n' giữa các đoạn.
 * @param basePitchHint       > 0 → ghim nhịp dòng chuẩn (đo từ frame ổn định).
 * @param paragraphRatioHint  > 0 → ghim tỉ lệ khe đoạn theo nhịp chuẩn.
 */
[[nodiscard]] QString assemble(const QList<OcrTextBox>& boxes, int basePitchHint = 0,
                               double paragraphRatioHint = 0.0);

/** Một rect bao chặt cho mỗi dòng chữ. */
[[nodiscard]] QList<QRect> lineRects(const QList<OcrTextBox>& boxes);

/** Như assemble() nhưng giữ lại từng dòng kèm rect. */
[[nodiscard]] QList<LineText> assembleLines(const QList<OcrTextBox>& boxes,
                                            int basePitchHint = 0);

/** Như assembleLines() nhưng trả cả text lẫn dòng trong một lượt ghép. */
[[nodiscard]] Assembled assembleDocument(const QList<OcrTextBox>& boxes, int basePitchHint = 0,
                                         double paragraphRatioHint = 0.0);

/** Nhịp dòng chuẩn (khoảng cách đỉnh-dòng, miễn nhiễm với unclip của det). */
[[nodiscard]] int estimateLinePitch(const QList<OcrTextBox>& boxes);

/** Khe đoạn dưới dạng bội số của nhịp chuẩn (0 nếu không có đoạn). */
[[nodiscard]] double estimateParagraphRatio(const QList<OcrTextBox>& boxes);

/**
 * @brief Cắt các box của MỘT hàng thị giác thành cụm theo khe ngang.
 *
 * Một hàng có thể chứa nhiều văn bản độc lập: thanh tab (排位赛 | 挑战赛 | 排行榜) hay
 * nhãn + giá trị ở tận bên phải. Dịch cả hàng thành một câu sẽ ra câu gộp không ai hỏi,
 * nên hover chỉ dịch CỤM mà con trỏ đang ở. Trong một cụm thì các box được nối lại — đó
 * là thứ giữ nguyên một câu mà detector cắt thành nhiều mảnh: chữ CJK không có dấu cách
 * nên các mảnh cách nhau vài mm, còn hai văn bản khác nhau cách nhau vài chục px.
 *
 * Quy tắc khe giống đường layout dùng cho cột: box cách box trước hơn ~1 nhịp dòng thì
 * mở cụm mới.
 *
 * @return với mỗi cụm theo thứ tự trái→phải, danh sách chỉ số (vào `rects`).
 */
[[nodiscard]] QList<QList<int>> clusterRow(const QList<QRect>& rects, int gapThreshold);

/**
 * @brief Chỉ số box mà con trỏ "trúng", với vùng trúng được MỞ RỘNG tới KHE với box kề.
 *
 * Port ý tưởng từ meikipop (`hit_scan.is_in_box_ex`): vùng trúng của một box được nới
 * NGANG ra tới giữa khoảng đệm giữa nó với box trước/sau (chữ dọc thì nới theo trục dọc).
 * Nhờ vậy:
 *  - không có "vùng chết" ở khe giữa hai phần tử;
 *  - biên vùng nhận do KHOẢNG ĐỆM tới phần tử khác quyết định, không phải bề rộng cố định;
 *  - trỏ lệch ra ngoài glyph một chút vẫn nhận đúng phần tử đó.
 *
 * meikipop nới hết cả khe (min/max) nên phần tử đứng trước "thắng" cả khe; ở đây lấy
 * ĐIỂM GIỮA để phần tử gần con trỏ hơn thắng — trực giác hơn khi chọn câu để dịch.
 *
 * @return chỉ số trong `rects` (phải sắp theo trục đọc), -1 nếu không trúng.
 */
[[nodiscard]] int hitRegionIndex(const QList<QRect>& rects, const QPoint& point,
                                 bool vertical = false);

/**
 * @brief Nhóm các box thành ĐOẠN theo lân cận (port từ meikipop
 *        `group_lines_into_paragraphs`).
 *
 * Hai dòng thuộc cùng đoạn khi chồng lấn theo trục đọc > 0.5 × cạnh nhỏ VÀ khoảng cách
 * tâm theo trục vuông góc < 1.9 × cạnh lớn. Nhóm theo kiểu BẮC CẦU: một dòng mới chỉ
 * cần lân cận với BẤT KỲ dòng nào đã có trong nhóm (đoạn xuống dòng so le vẫn gom đủ).
 *
 * Đây là thứ quyết định "1 đoạn văn": đơn vị đọc/dịch là cả khối, không phải từng dòng.
 *
 * @param rects phải sắp theo thứ tự đọc (trên→dưới, trái→phải).
 * @return danh sách chỉ số (vào `rects`) cho từng đoạn, mỗi đoạn đã sắp thứ tự đọc.
 */
[[nodiscard]] QList<QList<int>> groupParagraphs(const QList<QRect>& rects);

/**
 * @brief Sửa lỗi model đọc glyph SPACE thành dấu nháy đơn (artifact của font game).
 * Chỉ sửa khi dòng thật sự có artifact (≥ 2 dấu nháy không phải contraction).
 */
[[nodiscard]] QString repairApostropheSpacing(const QString& text);

/** True when a source line ends a sentence (ignores trailing quotes/brackets). */
[[nodiscard]] bool endsWithSentenceEnd(const QString& text);

} // namespace OcrTextAssembler
} // namespace EZTranslator
