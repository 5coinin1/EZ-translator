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
 * @brief Sửa lỗi model đọc glyph SPACE thành dấu nháy đơn (artifact của font game).
 * Chỉ sửa khi dòng thật sự có artifact (≥ 2 dấu nháy không phải contraction).
 */
[[nodiscard]] QString repairApostropheSpacing(const QString& text);

} // namespace OcrTextAssembler
} // namespace EZTranslator
