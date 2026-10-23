#pragma once

#include <QImage>
#include <QList>
#include <QRect>

// Rebuilds the background under recognized text.
//
// This is a port of the user's previous app (RSTGameTranslation, src/TextEraser.cs): the strokes
// are found with an ADAPTIVE threshold, grown into the patch margin (OCR boxes are tight and cut
// descenders/accents off), inpainted from the surrounding pixels (outside-in fill + smoothing) and
// the patch fades to transparent at its border. The result keeps the texture, gradient or art of
// the UI underneath instead of a flat box, and neighbouring strokes are not smeared into the fill.
namespace TextCover {

// `boxes` are in `crop` coordinates. Returns a copy of `crop` (Format_RGB32) with the text inside
// the boxes replaced by the surrounding background.
QImage removeText(const QImage &crop, const QList<QRect> &boxes);

// Same, but takes a rectangle of `region`: the crop is taken and returned, so the
// caller can use the result as the cover plate for that rectangle. The plate is opaque over the
// text boxes and over the erased strokes in their margin, and fades out across the margin.
QImage cleanPlate(const QImage &region, const QRect &area, const QList<QRect> &boxes);

} // namespace TextCover
