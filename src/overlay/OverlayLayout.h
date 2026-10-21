#pragma once

#include <QColor>
#include <QImage>
#include <QList>
#include <QRect>
#include <QStringList>

#include "overlay/TextOverlay.h"
#include "translation/DocumentTranslator.h"

/**
 * @brief Dựng layout overlay từ tài liệu dịch + các dòng OCR — DÙNG CHUNG cho app và tool test
 *        (tools/overlay_probe) để tool kiểm tra ĐÚNG code thật, không phải bản sao.
 */
namespace EZTranslator::OverlayLayout {

/**
 * @brief Dựng một box dịch cho mỗi ĐOẠN NGUỒN, đúng placement thường của translator.
 *
 * Giữ thứ tự đoạn nguồn; text dịch tự wrap trong vùng đoạn, không chia thủ công theo
 * tỷ lệ ký tự vào từng dòng (cách đó cắt câu giữa cụm từ và làm bản dịch rất tệ).
 * Chỉ các glyph OCR gốc bị che; khoảng trắng/đệm vẫn nguyên trạng.
 */
QList<OverlayBox> buildParagraphBoxes(const TranslatedDocument& doc, const QList<QRect>& lines,
                                      const QStringList& lineTexts,
                                      const QRect& regionBounds = {},
                                      bool skipUntranslated = false);

/**
 * @brief One reading paragraph detected from OCR lines, before translation.
 *
 * Novel mode works on paragraphs, not individual OCR boxes. A paragraph keeps
 * all source line rects so the renderer can cover exactly those lines while
 * using the paragraph slot for the translated text.
 */
struct NovelParagraph
{
    QRect        rect;       ///< union of source line rects
    QList<QRect> lineRects;  ///< source lines to cover
    QString      text;       ///< source text joined into one paragraph
    int          lineHeight{0};
};

/**
 * @brief Heuristic detector for single-column reading layouts (novels, articles).
 *
 * Requires several lines, long median text and line widths that fill a good part
 * of the region. UI layouts normally fail this and keep the box layout.
 */
[[nodiscard]] bool looksLikeNovel(const QList<QRect>& lineRects,
                                  const QStringList& lineTexts,
                                  const QRect& regionBounds,
                                  bool forceNovel = false);

/**
 * @brief Group OCR lines into paragraphs using vertical gaps / first-line indent.
 */
[[nodiscard]] QList<NovelParagraph> groupNovelParagraphs(const QList<QRect>& lineRects,
                                                         const QStringList& lineTexts);

/**
 * @brief Build paper-mode overlay boxes: one paragraph slot, full source-line cover.
 *
 * The paragraph slot extends to the next paragraph so the translated text can use
 * paragraph spacing without overlapping the next block. Only source line glyphs
 * are covered; the page background between lines stays intact.
 */
QList<OverlayBox> buildNovelBoxes(const TranslatedDocument& doc,
                                  const QList<NovelParagraph>& paragraphs,
                                  const QRect& regionBounds = {});

/**
 * @brief Màu chữ tương phản với NỀN của vùng: nền sáng → chữ gần đen, nền tối → chữ gần trắng.
 *
 * Overlay trước đây luôn vẽ chữ TRẮNG trên nền che tối. Khi cover đổi sang smart-plate (giữ
 * nguyên nền gốc), trang sáng + chữ trắng = "trắng xóa". Lấy trung vị độ sáng của ảnh ROI làm mốc.
 */
QColor contrastTextColor(const QImage& regionImage);

/**
 * @brief Màu NỀN đại diện của vùng (trung vị từng kênh RGB của ảnh thu nhỏ).
 *
 * Dùng làm màu che TẠM khi plate chưa dựng xong: mặc định cũ là navy đậm {17,22,34} nên trên
 * trang sáng người dùng thấy "cover đen" nhấp nháy rồi mới thành nền thật. Che bằng màu nền
 * thật thì mắt gần như không thấy chuyển tiếp.
 */
QColor regionCoverColor(const QImage& regionImage);

/**
 * @brief Background color for paper/reading mode.
 *
 * Uses a bright percentile instead of the median: a page with a lot of dark
 * text and gray sidebars still has a white reading area, and covering a white
 * paragraph with the median gray would leave visible gray slabs.
 */
QColor regionPaperColor(const QImage& regionImage);

} // namespace EZTranslator::OverlayLayout
