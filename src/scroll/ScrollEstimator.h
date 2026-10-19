#pragma once

#include <QImage>
#include <QList>
#include <QRect>

// Cheap 1D signature of a region: one value per row and per column. Small enough to
// compare two frames without touching pixels, which is what lets us estimate how far
// the content scrolled between frames (and then reuse the previous OCR result).
//
// Two signals are kept, computed in the same pixel pass:
//   - luminance: mean brightness per row/column. Aligns a smooth page even when it
//     has no sharp edges, but it is a weak signal on low contrast text.
//   - gradient: mean |d/dy| per row (|d/dx| per column). Text is a texture of strokes,
//     so this stays sharp no matter how bright or dark the page is, and it is not
//     fooled by a background gradient.
//
// `matchProfile` tries luminance first and falls back to the gradient signal, so the
// cheap case stays cheap and the hard case still aligns.
struct GrayProfile
{
    QList<double> rows;
    QList<double> cols;
    QList<double> edgeRows;
    QList<double> edgeCols;

    bool isEmpty() const { return rows.isEmpty() || cols.isEmpty(); }
};

struct ScrollOffset
{
    int dx = 0;  // > 0 : content moved right by dx pixels
    int dy = 0;  // > 0 : content moved down by dy pixels

    // Sub-pixel estimate (the correlation peak interpolated between samples). The
    // integer fields above are this value rounded, so they stay usable for shifting
    // rectangles while the tracker keeps the fractional part and does not drift.
    double dxExact = 0.0;
    double dyExact = 0.0;

    double score = 0.0;    // 1.0 = identical signal over the overlapping area
    bool valid = false;    // score >= minScore (and a usable overlap existed)
    bool usedGradient = false; // the gradient signal won (luminance was not enough)
};

// `withGradients` also fills edgeRows/edgeCols (a second read per sample, so only the
// scrolling path asks for it).
GrayProfile grayProfile(const QImage &image, const QRect &rect, int step = 4,
                        bool withGradients = false);

// Finds the shift that best aligns `current` onto `previous`. Scored with a zero-mean
// normalized cross correlation (so a brightness or contrast change does not move the
// peak) and refined with a parabolic fit -> sub-pixel result.
// maxShift <= 0 means "one third of the axis length". minScore gates validity.
// preferredShift is the expected shift (for example the velocity model of the scroll
// tracker). When several shifts explain the frames equally well - which happens with
// regularly spaced text lines - the one closest to it wins, instead of an aliased copy
// several line pitches away.
ScrollOffset matchProfile(const GrayProfile &previous, const GrayProfile &current,
                          int maxShift = 0, double minScore = 0.90,
                          double preferredShift = 0.0);
