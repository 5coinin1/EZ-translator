#pragma once

#include <QImage>
#include <QList>
#include <QRect>

#include <functional>

#include "scroll/ScrollEstimator.h"
#include "scroll/ScrollTracker.h"
#include "ocr/OcrTypes.h"

// OcrTextBox của EZ nằm trong namespace EZTranslator; module này giữ nguyên code gốc (global).
using OcrTextBox = EZTranslator::OcrTextBox;

// Temporal reuse for a scrolling region (the "DLSS-like" trick): instead of
// re-running OCR on every frame while the user scrolls, estimate how far the
// content moved and translate the previous OCR boxes by that amount, then OCR
// only the thin band of newly exposed pixels. The result is a translated overlay
// that follows the scroll with almost no per-frame cost.
class ScrollOcrReuse
{
public:
    // Given an image crop, return boxes in that crop's coordinates.
    using OcrFn = std::function<QList<OcrTextBox>(const QImage &crop)>;

    struct IncrementalResult
    {
        bool ok = false;
        int dx = 0;
        int dy = 0;
        double score = 0.0;
        QList<OcrTextBox> boxes;
    };

    // If a confident scroll shift versus the last accepted state exists, shift the
    // previous boxes, OCR only the exposed band and merge. Otherwise ok == false.
    IncrementalResult tryIncremental(const QImage &frame, const QRect &rect, const OcrFn &bandOcr);

    // Remember a fully (re)computed state to base future increments on.
    void accept(const QImage &frame, const QRect &rect, const QList<OcrTextBox> &boxes);

    void reset();

    bool hasState() const { return m_valid; }
    QList<OcrTextBox> boxes() const { return m_boxes; }

private:
    ScrollTracker m_tracker;
    GrayProfile m_profile;
    QList<OcrTextBox> m_boxes;
    bool m_valid = false;
};
