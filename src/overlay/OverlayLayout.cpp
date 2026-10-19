#include "overlay/OverlayLayout.h"

#include <algorithm>

#include "overlay/ParagraphAnchor.h"
#include "overlay/TextFlow.h"

namespace EZTranslator::OverlayLayout {

QList<OverlayBox> buildParagraphBoxes(const TranslatedDocument& doc, const QList<QRect>& lines,
                                      const QStringList& lineTexts, const QRect& regionBounds,
                                      bool skipUntranslated)
{
    QList<OverlayBox> boxes;
    // Neo đoạn theo NỘI DUNG (ParagraphAnchor — port từ bản translator): vị trí đoạn cũ dùng chỉ
    // số dòng cộng dồn, nên assembler gộp/tách/rơi MỘT dòng là mọi đoạn sau lệch một dòng và không
    // gì phát hiện được. Khớp chữ nguồn của đoạn với các dòng thật thì đoạn tự kéo về đúng chỗ.
    const QList<int> paragraphStarts =
        ParagraphAnchor::anchor(doc.sourceParagraphs, lineTexts, doc.lineCounts);
    int line = 0;
    for (int p = 0; p < doc.sourceParagraphs.size(); ++p) {
        const int count = doc.lineCounts.value(p, 1);
        if (p < paragraphStarts.size()) {
            // Sàn = đoạn TRƯỚC + 1: neo cho phép lùi trong cửa sổ, nhưng nếu trả cùng một dòng cho
            // hai đoạn liền nhau thì hai box CHỒNG NHAU (chữ đè lên nhau — probe ảnh 16 bắt được).
            const int lower = p > 0 ? paragraphStarts.at(p - 1) + 1 : 0;
            line = qBound(lower, paragraphStarts.at(p), qMax(0, int(lines.size()) - 1));
        }
        const int firstLine = line;
        const int endLine =
            (p + 1 < paragraphStarts.size())
                ? qBound(firstLine + 1, paragraphStarts.at(p + 1), int(lines.size()))
                : qMin(lines.size(), firstLine + count);
        line = endLine;

        if (firstLine >= endLine)
            continue;

        QRect sourceSpan;
        QList<QRect> coverRects;
        int firstLineHeight = 0;
        for (int i = firstLine; i < endLine; ++i) {
            const QRect& sourceLine = lines.at(i);
            sourceSpan = sourceSpan.isNull() ? sourceLine : sourceSpan.united(sourceLine);
            coverRects.append(sourceLine);
            if (firstLineHeight <= 0)
                firstLineHeight = sourceLine.height();
        }

        QString text = doc.paragraphs.value(p).trimmed();
        if (text.isEmpty()) {
            if (skipUntranslated)
                continue; // chưa có bản dịch (emit cache-only) → để TRỐNG, không hiện bản nguồn
            text = doc.sourceParagraphs.value(p).simplified(); // lưới an toàn 8s
        }
        if (sourceSpan.isEmpty() || text.isEmpty())
            continue;

        // Paragraph box occupies the source paragraph's vertical slot: from its first
        // source line to the next paragraph's first line (or ROI bottom). Horizontally
        // it may use the remaining ROI width, matching the reference renderer.
        const int top = lines.at(firstLine).top();
        int bottom = sourceSpan.bottom() + 1;
        if (endLine < lines.size())
            bottom = qMax(bottom, lines.at(endLine).top());
        else if (regionBounds.isValid())
            bottom = qMax(bottom, regionBounds.bottom());

        int right = sourceSpan.right() + 1;
        if (regionBounds.isValid())
            right = qMax(right, regionBounds.right() - 3);

        OverlayBox box;
        box.rect = QRect(sourceSpan.left(), top, qMax(8, right - sourceSpan.left()),
                         qMax(8, bottom - top));
        box.coverRects = coverRects;
        box.text = text;
        box.fontPixelSize = qMax(9, int(firstLineHeight * 0.65));
        boxes.append(box);
    }

    // KHÔNG để hai box CÙNG HÀNG (khác cột) chồng lên nhau: box bên trái bị chặn mép phải tại mép
    // trái của box kế tiếp trên cùng hàng. UI nhiều cột (sidebar ↔ panel, nhãn ↔ dropdown) trước
    // đây cả hai kéo dài tới mép vùng nên chữ hai cột GHI ĐÈ lên nhau ("cùng hàng bị ghi vào nhau").
    if (boxes.size() > 1) {
        const int regionRight =
            regionBounds.isValid() ? regionBounds.right() - 3 : boxes.first().rect.right();
        for (int i = 0; i < boxes.size(); ++i) {
            int limit = regionRight;
            for (int j = 0; j < boxes.size(); ++j) {
                if (i == j)
                    continue;
                const QRect& other = boxes.at(j).rect;
                if (other.left() <= boxes.at(i).rect.left())
                    continue; // chỉ chặn bởi box nằm BÊN PHẢI
                const int overlapTop = qMax(other.top(), boxes.at(i).rect.top());
                const int overlapBottom = qMin(other.bottom(), boxes.at(i).rect.bottom());
                const int overlapHeight = overlapBottom - overlapTop;
                const int minHeight = qMin(other.height(), boxes.at(i).rect.height());
                // "Cùng hàng" thật = chồng nhau ≥50% chiều cao; chỉ chạm mép vài px thì không tính
                // (nếu không một box cao hơn sẽ chặn box dưới còn vài px — box[6] image7).
                if (overlapHeight <= 0 || overlapHeight * 2 < minHeight)
                    continue;
                limit = qMin(limit, other.left() - 6);
            }
            boxes[i].rect.setRight(qMax(boxes[i].rect.left() + 8, limit));
        }
    }

    // Fit cỡ chữ (TextFlow — port từ bản translator): MỘT cỡ chung cho cả vùng, hạ xuống chỉ khi
    // đoạn nào đó tràn khung. Box quá thấp để chứa một dòng (thoái hoá ở rìa vùng) bị loại khỏi
    // phép fit — nếu không, một box xấu kéo cả trang xuống cỡ tí xíu (bài học r7.78). Sàn 70% cỡ đo
    // từ dòng gốc để trang không bao giờ nhỏ li ti.
    if (!boxes.isEmpty()) {
        QList<int> sizes;
        for (const OverlayBox& box : boxes)
            sizes.append(box.fontPixelSize);
        std::sort(sizes.begin(), sizes.end());
        const int baseSize = qMax(9, sizes.at(sizes.size() / 2));

        QList<TextFlow::Paragraph> paragraphs;
        paragraphs.reserve(boxes.size());
        for (const OverlayBox& box : boxes) {
            if (box.rect.height() < int(baseSize * 1.2))
                continue; // quá thấp để chứa một dòng: không cho vào fit
            paragraphs.append(TextFlow::Paragraph{QRectF(box.rect), box.text});
        }
        if (!paragraphs.isEmpty()) {
            QFont fitFont;
            fitFont.setPixelSize(baseSize);
            const TextFlow::Result fit = TextFlow::fitParagraphs(paragraphs, fitFont, baseSize);
            int drawSize = fit.fontPixelSize > 0 ? fit.fontPixelSize : baseSize;
            drawSize = qMax(drawSize, qMax(9, int(baseSize * 0.7)));
            for (OverlayBox& box : boxes)
                box.fontPixelSize = drawSize;
        }
    }
    return boxes;
}

QColor contrastTextColor(const QImage& regionImage)
{
    if (regionImage.isNull())
        return {};
    const QImage small =
        regionImage.scaled(16, 16, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QList<int> luma;
    luma.reserve(small.width() * small.height());
    for (int y = 0; y < small.height(); ++y) {
        for (int x = 0; x < small.width(); ++x)
            luma.append(qGray(small.pixel(x, y)));
    }
    if (luma.isEmpty())
        return {};
    std::sort(luma.begin(), luma.end());
    return luma.at(luma.size() / 2) > 128 ? QColor(16, 16, 16) : QColor(245, 245, 245);
}

QColor regionCoverColor(const QImage& regionImage)
{
    if (regionImage.isNull())
        return {};
    const QImage small =
        regionImage.scaled(16, 16, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    if (small.isNull())
        return {};

    QList<int> reds;
    QList<int> greens;
    QList<int> blues;
    const int count = small.width() * small.height();
    reds.reserve(count);
    greens.reserve(count);
    blues.reserve(count);
    for (int y = 0; y < small.height(); ++y) {
        for (int x = 0; x < small.width(); ++x) {
            const QColor pixel = small.pixelColor(x, y);
            reds.append(pixel.red());
            greens.append(pixel.green());
            blues.append(pixel.blue());
        }
    }
    std::sort(reds.begin(), reds.end());
    std::sort(greens.begin(), greens.end());
    std::sort(blues.begin(), blues.end());
    return QColor(reds.at(reds.size() / 2), greens.at(greens.size() / 2),
                  blues.at(blues.size() / 2));
}

} // namespace EZTranslator::OverlayLayout
