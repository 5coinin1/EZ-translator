#include "overlay/TextFlow.h"

#include <QFontMetrics>

#include <algorithm>

namespace {

// The renderers inset the box by 1 px (a percentage padding used to push the text visibly
// inward from the original glyphs, which the user immediately noticed), so this is the room a
// paragraph really has.
constexpr double kInset = 1.0;

double horizontalRoom(const QRectF &rect)
{
    return std::max(8.0, rect.width() - 2.0 * kInset);
}

double verticalRoom(const QRectF &rect)
{
    return std::max(8.0, rect.height() - 2.0 * kInset);
}

int neededHeight(const QFontMetrics &metrics, double width, const QString &text)
{
    // A tall limit so word wrap is not bounded vertically: this is the height the text
    // really needs at that width.
    const QRect box(0, 0, std::max(8, int(width)), 100000);
    return metrics.boundingRect(box, Qt::TextWordWrap, text).height();
}

} // namespace

TextFlow::Result TextFlow::fitParagraphs(const QList<Paragraph> &paragraphs, const QFont &font,
                                         int startPixelSize)
{
    Result result;
    result.paragraphs = paragraphs;

    int pixel = std::max(6, startPixelSize);
    bool complete = false;
    // A handful of coarse steps is enough: the needed size is close to proportional to the
    // overflow, and each measurement is a cheap QFontMetrics call.
    for (int attempt = 0; attempt < 24; ++attempt) {
        QFont probe = font;
        probe.setPixelSize(pixel);
        const QFontMetrics metrics(probe);

        complete = true;
        for (const Paragraph &paragraph : paragraphs) {
            if (paragraph.text.isEmpty())
                continue;
            const double room = verticalRoom(paragraph.rect);
            if (neededHeight(metrics, horizontalRoom(paragraph.rect), paragraph.text) > room) {
                complete = false;
                break;
            }
        }

        result.fontPixelSize = pixel;
        if (complete || pixel <= 6)
            break;
        const int next = std::max(6, int(pixel * 0.93));
        if (next >= pixel)
            break;
        pixel = next;
    }

    result.complete = complete;
    return result;
}
