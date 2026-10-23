#pragma once

#include <QFont>
#include <QList>
#include <QRectF>
#include <QString>

// Places the translated text back onto the SOURCE geometry.
//
// One BOX per source PARAGRAPH, positioned where that paragraph was on screen, so the
// translation lines up with the original document. Inside a box the text is justified and
// wraps wherever it needs to: the box is as wide as the source paragraph, so the line
// breaks come from the translation itself and read like a normal paragraph, instead of
// following the source's line ends (which are arbitrary for a different language and leave
// every line half empty).
//
// Every box of a layout is drawn with ONE font size. When the translation needs more
// vertical room than the source had, the size is reduced for ALL boxes together - never
// for a single box, otherwise the sizes come out small and inconsistent (R2.58/R2.59).
namespace TextFlow {

struct Paragraph
{
    QRectF rect; // reference coordinates, like the overlay uses
    QString text;
};

struct Result
{
    QList<Paragraph> paragraphs; // same order as the input
    int fontPixelSize = 0;       // the single size every paragraph must be drawn at
    bool complete = false;       // false only when even the smallest size could not fit
};

// Fits every paragraph into its own rect at a single font size, shrinking the size for all
// of them together while any of them would overflow its slot. Measuring uses the same
// font, wrapping width and padding the renderers apply, so a box that fits here cannot be
// clipped on screen.
Result fitParagraphs(const QList<Paragraph> &paragraphs, const QFont &font, int startPixelSize);

} // namespace TextFlow
