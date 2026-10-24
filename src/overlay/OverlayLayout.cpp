#include "overlay/OverlayLayout.h"

#include <algorithm>

#include "overlay/ParagraphAnchor.h"
#include "translation/ProtectedText.h"

#include <QStringList>

#include <limits>

namespace EZTranslator::OverlayLayout {

namespace {

int medianInt(QList<int> values)
{
    if (values.isEmpty())
        return 0;
    std::sort(values.begin(), values.end());
    return values.at(values.size() / 2);
}

bool isCjkChar(QChar c)
{
    const ushort u = c.unicode();
    return (u >= 0x3400 && u <= 0x4DBF) || (u >= 0x4E00 && u <= 0x9FFF)
           || (u >= 0xF900 && u <= 0xFAFF) || (u >= 0x3040 && u <= 0x30FF)
           || (u >= 0xAC00 && u <= 0xD7AF);
}

bool endsSentence(const QString& text)
{
    QString trimmed = text.trimmed();
    static const QString closers = QStringLiteral("\"')]}\u201d\u2019\u00bb\u300d\u300f>");
    while (!trimmed.isEmpty() && closers.contains(trimmed.back()))
        trimmed.chop(1);
    if (trimmed.isEmpty())
        return false;
    const QChar last = trimmed.back();
    return last == QLatin1Char('.') || last == QLatin1Char('!') || last == QLatin1Char('?')
           || last == QChar(0x2026) || last == QChar(0x3002) || last == QChar(0xFF01)
           || last == QChar(0xFF1F);
}

} // namespace

bool looksLikeNovel(const QList<QRect>& lineRects, const QStringList& lineTexts,
                    const QRect& regionBounds, bool forceNovel)
{
    const QString mode =
        qEnvironmentVariable("EZ_OVERLAY_MODE", QStringLiteral("auto")).trimmed().toLower();
    if (mode == QLatin1String("novel") || mode == QLatin1String("reading"))
        return true;
    if (mode == QLatin1String("ui") || mode == QLatin1String("box"))
        return false;
    if (forceNovel)
        return true;
    const QString boxMode =
        qEnvironmentVariable("EZ_OVERLAY_BOX_LAYOUT", QStringLiteral("auto")).trimmed().toLower();
    if (boxMode == QLatin1String("1") || boxMode == QLatin1String("on")
        || boxMode == QLatin1String("box"))
        return false;

    if (lineRects.size() < 4 || lineRects.size() != lineTexts.size())
        return false;

    QList<int> widths;
    QList<int> lengths;
    int cjk = 0;
    int chars = 0;
    int wideLines = 0;
    for (int i = 0; i < lineRects.size(); ++i) {
        const QRect& rect = lineRects.at(i);
        if (rect.isEmpty())
            continue;
        widths.append(rect.width());
        const QString text = lineTexts.at(i).simplified();
        lengths.append(text.size());
        for (const QChar c : text) {
            if (isCjkChar(c))
                ++cjk;
            if (!c.isSpace())
                ++chars;
        }
        if (regionBounds.isValid() && regionBounds.width() > 0
            && rect.width() * 100 >= regionBounds.width() * 30)
            ++wideLines;
    }
    if (widths.isEmpty() || lengths.isEmpty())
        return false;

    const bool cjkText = chars > 0 && cjk * 4 >= chars;
    const int minMedianLength = cjkText ? 12 : 28;
    if (medianInt(lengths) < minMedianLength)
        return false;

    return wideLines * 100 >= int(widths.size()) * 45;
}

QList<NovelParagraph> groupNovelParagraphs(const QList<QRect>& lineRects,
                                           const QStringList& lineTexts)
{
    QList<NovelParagraph> paragraphs;
    if (lineRects.isEmpty())
        return paragraphs;

    // assembler already sorts lines, but be defensive: build an index order so
    // paragraph detection never depends on caller ordering.
    QList<int> order;
    order.reserve(lineRects.size());
    for (int i = 0; i < lineRects.size(); ++i) {
        if (!lineRects.at(i).isEmpty())
            order.append(i);
    }
    // A page may contain two reading columns. Sort each column top-to-bottom
    // before assembling paragraphs, so adjacent rows in different columns
    // never become the same translated block.
    int columnSplit = 0;
    if (order.size() >= 4) {
        QList<int> lefts;
        QRect extent;
        for (int index : order) {
            lefts.append(lineRects.at(index).left());
            extent = extent.isNull() ? lineRects.at(index) : extent.united(lineRects.at(index));
        }
        std::sort(lefts.begin(), lefts.end());
        int largestGap = 0;
        for (int i = 1; i < lefts.size(); ++i) {
            const int gap = lefts.at(i) - lefts.at(i - 1);
            if (i >= 2 && lefts.size() - i >= 2 && gap > largestGap) {
                largestGap = gap;
                columnSplit = (lefts.at(i) + lefts.at(i - 1)) / 2;
            }
        }
        if (largestGap < qMax(48, extent.width() / 4))
            columnSplit = 0;
    }
    std::sort(order.begin(), order.end(), [&lineRects, columnSplit](int a, int b) {
        if (columnSplit && (lineRects.at(a).left() < columnSplit)
                                != (lineRects.at(b).left() < columnSplit))
            return lineRects.at(a).left() < columnSplit;
        if (lineRects.at(a).top() != lineRects.at(b).top())
            return lineRects.at(a).top() < lineRects.at(b).top();
        return lineRects.at(a).left() < lineRects.at(b).left();
    });
    if (order.isEmpty())
        return paragraphs;

    QList<int> heights;
    for (int index : order)
        heights.append(lineRects.at(index).height());
    const int lineHeight = qMax(8, medianInt(heights));
    // Normal line gap is usually < 0.3 line height; a blank paragraph gap is
    // noticeably larger. Keep the threshold conservative so a line is never
    // promoted to a new paragraph just because OCR shook its box by a few px.
    const int gapThreshold = qMax(4, int(std::lround(lineHeight * 0.72)));

    NovelParagraph current;
    QRect previousRect;
    QString previousText;
    bool previousSpecial = false;
    bool previousRightColumn = false;
    for (int index : order) {
        const QRect rect = lineRects.at(index);
        const QString text = lineTexts.value(index).simplified();
        const bool pageNumber = EZTranslator::isPageNumber(text);
        const bool code = EZTranslator::isProtectedCode(text);
        const bool heading = !code && !pageNumber && text.size() <= 90
                             && rect.height() >= lineHeight * 1.28;
        const bool special = code || pageNumber || heading;
        const bool rightColumn = columnSplit && rect.left() >= columnSplit;

        bool newParagraph = current.lineRects.isEmpty();
        if (!newParagraph) {
            const int gap = rect.top() - previousRect.bottom();
            const int indent = rect.left() - previousRect.left();
            const bool blankGap = gap > gapThreshold;
            const bool indented =
                gap > 2 && indent >= std::max(6, int(std::lround(lineHeight * 0.85)))
                && endsSentence(previousText);
            newParagraph = blankGap || indented || special || previousSpecial
                           || rightColumn != previousRightColumn;
        }
        if (newParagraph) {
            if (!current.lineRects.isEmpty())
                paragraphs.append(current);
            current = NovelParagraph();
            current.rect = rect;
            current.lineHeight = lineHeight;
            previousText.clear();
        } else {
            current.rect = current.rect.united(rect);
        }

        current.lineRects.append(rect);
        if (!text.isEmpty()) {
            if (!current.text.isEmpty()) {
                const QChar previous = current.text.back();
                const QChar next = text.front();
                if (!isCjkChar(previous) && !isCjkChar(next))
                    current.text += QLatin1Char(' ');
            }
            current.text += text;
        }
        previousRect = rect;
        previousSpecial = special;
        previousRightColumn = rightColumn;
        if (!text.isEmpty())
            previousText = text;
    }
    if (!current.lineRects.isEmpty())
        paragraphs.append(current);

    for (NovelParagraph& paragraph : paragraphs) {
        QList<int> memberHeights;
        for (const QRect& rect : paragraph.lineRects)
            memberHeights.append(rect.height());
        paragraph.lineHeight = qMax(8, medianInt(memberHeights));
    }
    return paragraphs;
}

QList<OverlayBox> buildNovelBoxes(const TranslatedDocument& doc,
                                  const QList<NovelParagraph>& paragraphs,
                                  const QRect& regionBounds)
{
    QList<OverlayBox> boxes;
    if (paragraphs.isEmpty())
        return boxes;

    // Column right edge: use the upper quartile of wide source lines, not the
    // maximum. A stray icon/button far to the right (or a centered title) would
    // otherwise stretch every paragraph across the whole page.
    QList<int> rights;
    const int minWide = regionBounds.isValid() ? qMax(24, regionBounds.width() / 5) : 24;
    // A right-side icon/button can be grouped into a text line by OCR. Ignore
    // boxes in the outer 10% when computing the reading-column edge.
    const int maxSaneRight = regionBounds.isValid()
                                 ? regionBounds.right() - qMax(24, regionBounds.width() / 10)
                                 : std::numeric_limits<int>::max();
    for (const NovelParagraph& paragraph : paragraphs) {
        for (const QRect& line : paragraph.lineRects) {
            if (line.width() < minWide)
                continue;
            const int right = line.right() + 1;
            if (right <= maxSaneRight || rights.isEmpty())
                rights.append(right);
        }
    }
    int columnRight = 0;
    if (!rights.isEmpty()) {
        std::sort(rights.begin(), rights.end());
        const int base = rights.at(qBound(0, int(rights.size() * 3 / 4), rights.size() - 1));
        const int pad = regionBounds.isValid() ? qMax(12, regionBounds.width() / 50) : 12;
        columnRight = base + pad;
        if (regionBounds.isValid())
            columnRight = qMin(columnRight, regionBounds.right() - 3);
    }
    if (columnRight <= 0 && regionBounds.isValid())
        columnRight = regionBounds.right() - 3;

    for (int p = 0; p < paragraphs.size(); ++p) {
        const NovelParagraph& paragraph = paragraphs.at(p);
        if (paragraph.rect.isEmpty())
            continue;

        const QString text = p < doc.paragraphs.size()
                                 ? doc.paragraphs.at(p).trimmed()
                                 : QString();
        if (text.isEmpty())
            continue;

        const int top = paragraph.rect.top();
        int bottom = paragraph.rect.bottom() + 1;
        const int nextTop = p + 1 < paragraphs.size()
                                && paragraphs.at(p + 1).rect.top() > top
                                ? paragraphs.at(p + 1).rect.top()
                                : (regionBounds.isValid() ? regionBounds.bottom() + 1 : bottom);
        bottom = qMax(bottom, qMin(nextTop, bottom + qMax(2, paragraph.lineHeight / 2)));

        const int left = paragraph.rect.left();
        const bool fixedWidth = EZTranslator::isProtectedCode(paragraph.text)
                                || EZTranslator::isPageNumber(paragraph.text);
        int localRight = columnRight;
        for (const NovelParagraph& other : paragraphs) {
            if (other.rect.left() > left + qMax(48, paragraph.lineHeight * 4)
                && other.rect.top() < bottom && other.rect.bottom() >= top)
                localRight = qMin(localRight, other.rect.left() - 6);
        }
        const int right = qMax(left + 8, fixedWidth ? paragraph.rect.right() + 1 : localRight);
        OverlayBox box;
        box.rect = QRect(left, top, right - left, qMax(8, bottom - top));
        box.coverRects = paragraph.lineRects;
        box.text = text;
        box.fontPixelSize = qMax(9, int(std::lround(paragraph.lineHeight * 0.72)));
        box.fitToBox = true;
        boxes.append(box);
    }
    return boxes;
}

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
        const int nextTop = endLine < lines.size()
                                ? lines.at(endLine).top()
                                : (regionBounds.isValid() ? regionBounds.bottom() + 1 : bottom);
        bottom = qMax(bottom, qMin(nextTop, bottom + qMax(2, firstLineHeight / 2)));

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

QColor regionPaperColor(const QImage& regionImage)
{
    if (regionImage.isNull())
        return {};
    const QImage small =
        regionImage.scaled(32, 32, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    if (small.isNull())
        return {};

    QList<QColor> pixels;
    pixels.reserve(small.width() * small.height());
    for (int y = 0; y < small.height(); ++y) {
        for (int x = 0; x < small.width(); ++x)
            pixels.append(small.pixelColor(x, y));
    }
    if (pixels.isEmpty())
        return {};
    std::sort(pixels.begin(), pixels.end(), [](const QColor& a, const QColor& b) {
        return qGray(a.rgb()) < qGray(b.rgb());
    });
    return pixels.at(qBound(0, int(pixels.size() * 0.95), pixels.size() - 1));
}

} // namespace EZTranslator::OverlayLayout
