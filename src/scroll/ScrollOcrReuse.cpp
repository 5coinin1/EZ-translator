#include "scroll/ScrollOcrReuse.h"

#include <cmath>

namespace {
// Sampling step of the 1D signature. One entry per row/column (that is what resolves a
// 1 px scroll); the step only thins the samples *inside* a row, and a line is 20+ px
// tall, so 8 is plenty and halves the pixel reads of the hottest loop.
constexpr int kProfileStep = 8;

// A score above the strict threshold is an unambiguous match. Real captured frames
// often land slightly below it because of animation and antialiasing, so allow a
// relaxed score only after the horizontal/page-jump/no-motion guards below confirm
// that this is a plausible vertical scroll.
constexpr double kStrictScore = 0.96;
constexpr double kVerticalScrollScore = 0.92;
constexpr int kBandPadding = 48;
} // namespace

void ScrollOcrReuse::reset()
{
    m_profile = GrayProfile();
    m_boxes.clear();
    m_valid = false;
    m_tracker.reset();
}

void ScrollOcrReuse::accept(const QImage &frame, const QRect &rect, const QList<OcrTextBox> &boxes)
{
    m_tracker.reset();
    m_profile = grayProfile(frame, rect, kProfileStep, /*withGradients=*/true);
    m_boxes = boxes;
    m_valid = !m_profile.isEmpty();
}

ScrollOcrReuse::IncrementalResult ScrollOcrReuse::tryIncremental(const QImage &frame,
                                                                 const QRect &rect,
                                                                 const OcrFn &bandOcr)
{
    IncrementalResult result;
    if (!m_valid)
        return result;

    const GrayProfile current = grayProfile(frame, rect, kProfileStep, /*withGradients=*/true);
    if (current.isEmpty() || current.rows.size() != m_profile.rows.size()
        || current.cols.size() != m_profile.cols.size()) {
        result.reason = "size-changed";
        return result;
    }

    // Text pages are noisy (antialiasing, subpixel shifts), so the match is allowed to
    // be a bit looser; the band OCR corrects whatever the estimate misses.
    //
    // Once the motion model is running the search is narrowed around its prediction.
    // Two reasons: it is cheaper, and - more important - evenly spaced text lines
    // correlate just as well one line pitch away, so a wide window likes to lock onto
    // an aliased copy. A prediction keeps that from happening; if the scroll changes
    // speed the score simply drops and a full OCR runs instead.
    const double predicted = m_tracker.predictedStep();
    const int maxShift = m_tracker.hasState()
                             ? std::max(12, int(std::abs(predicted) + 24.0))
                             : std::max(rect.width(), rect.height()) / 2;
    const ScrollOffset offset =
        matchProfile(m_profile, current, maxShift, kVerticalScrollScore, predicted,
                     m_tracker.hasState());
    result.score = offset.score;
    result.dx = offset.dx;
    result.dy = offset.dy;
    if (!offset.valid) {
        result.reason = "low-score";
        return result;
    }
    if (std::abs(offset.dx) > 2) {
        result.reason = "horizontal-shift";
        return result;
    }
    if (std::abs(offset.dy) > rect.height() / 3) {
        result.reason = "page-jump";
        return result;
    }
    if (offset.dy == 0) {
        result.reason = offset.score >= kStrictScore ? "no-vertical-shift"
                                                     : "relaxed-no-vertical-shift";
        return result;
    }

    // Filter the noisy per-frame measurement through the motion model. It smooths the
    // shift, carries the sub-pixel remainder (so a slow scroll does not round to zero
    // every frame) and keeps the tracking alive for the frame or two where the matcher
    // cannot align. A residual beyond a third of the region is a page jump, not a
    // scroll, and restarts the model.
    ScrollTracker tracker = m_tracker;
    const ScrollTracker::Step step =
        tracker.update(offset.dyExact, true, rect.height() / 3.0);
    if (!step.valid) {
        result.reason = "motion-model";
        return result;
    }

    const int appliedDy = step.dy;
    const int appliedDx = int(std::lround(offset.dxExact));
    if (appliedDy == 0 || appliedDx != 0) {
        result.reason = "no-vertical-shift";
        return result;
    }

    QList<OcrTextBox> merged = m_boxes;
    for (OcrTextBox &box : merged)
        box.rect.translate(appliedDx, appliedDy);

    QList<QRect> bands;
    if (appliedDy > 0)
        bands.append(QRect(0, 0, rect.width(), qMin(rect.height(), appliedDy + kBandPadding)));
    else if (appliedDy < 0)
        bands.append(QRect(0, qMax(0, rect.height() + appliedDy - kBandPadding),
                           rect.width(), qMin(rect.height(), -appliedDy + kBandPadding)));

    // Replace lines near the exposed edge too: a line may have been clipped on the
    // previous frame and must be recognized again once fully visible.
    for (const QRect &band : bands) {
        for (auto it = merged.begin(); it != merged.end();) {
            if (it->rect.intersects(band.translated(rect.topLeft())))
                it = merged.erase(it);
            else
                ++it;
        }
    }

    for (const QRect &band : bands) {
        const QRect absolute = band.translated(rect.topLeft());
        const QImage crop = frame.copy(absolute);
        const QList<OcrTextBox> found = bandOcr(crop);
        for (OcrTextBox box : found) {
            box.rect.translate(absolute.topLeft());
            if (rect.contains(box.rect))
                merged.append(box);
        }
    }

    QList<OcrTextBox> inside;
    for (const OcrTextBox &box : merged) {
        if (box.text.isEmpty())
            continue;
        if (rect.contains(box.rect))
            inside.append(box);
    }

    // An empty merge (every shifted box left the region and the band OCR found
    // nothing) must NOT be accepted, otherwise it would overwrite the state and
    // the layout would stay blank from then on.
    if (inside.isEmpty()) {
        result.reason = "no-visible-text";
        return result;
    }

    result.ok = true;
    result.reason = "reused";
    result.dx = appliedDx;
    result.dy = appliedDy;
    result.score = offset.score;
    result.boxes = inside;

    m_profile = current;
    m_boxes = inside;
    m_tracker = tracker;
    return result;
}
