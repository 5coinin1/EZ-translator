#include "scroll/ScrollTracker.h"

#include <algorithm>
#include <cmath>

namespace {

// Weight of the newest measurement in the velocity estimate (a simple EMA). The
// velocity is only used to coast through the frames where no measurement exists, so
// it must be stable rather than fast.
constexpr double kVelocityAlpha = 0.3;

// How many frames in a row the velocity model may carry the tracking when no
// measurement is available. Beyond that the state is stale enough to be discarded.
constexpr int kMaxMisses = 3;

// A coasted shift this large is not something we can follow reliably; require a
// measurement before reusing anything again.
constexpr double kMaxPredictedShift = 220.0;

// Tried and REVERTED here (R2.64/R2.65), so nobody re-tries it blindly: a constant-ACCELERATION
// coast (velocity + 0.5*accel, then velocity += accel) and a "raw last step" coast for the first
// dropped frame. Both were measured with `trackingFidelityUnderBob` (accelerating scroll plus a
// 2 Hz bob, 1 in 6 frames without a measurement): the acceleration model was clearly worse
// (4.4 px of drift against 2.4 px) because a bob alternates sign every frame, so the estimated
// acceleration is the bob's and extrapolating it across a dropout extrapolates the wrong thing.
// The plain EMA velocity stays.

} // namespace

void ScrollTracker::reset()
{
    m_position = 0.0;
    m_applied = 0.0;
    m_velocity = 0.0;
    m_misses = 0;
    m_inited = false;
}

ScrollTracker::Step ScrollTracker::update(double measuredDy, bool accepted, double jumpPx)
{
    Step step;

    if (!accepted) {
        if (!m_inited)
            return step; // nothing to coast on
        if (++m_misses > kMaxMisses) {
            reset();
            return step; // caller falls back to a full OCR
        }
        m_position += m_velocity; // coast
        step.predicted = true;
        if (std::abs(m_position - m_applied) > kMaxPredictedShift) {
            reset();
            return step;
        }
    } else {
        // A single huge step means the content was replaced (new page, page down),
        // not scrolled. Blending it would poison the velocity model, so restart and let
        // the caller rebuild the reference state.
        if (m_inited && jumpPx > 0.0 && std::abs(measuredDy) > jumpPx) {
            reset();
            return step;
        }
        m_inited = true;
        m_misses = 0;
        m_position += measuredDy;
        m_velocity = (1.0 - kVelocityAlpha) * m_velocity + kVelocityAlpha * measuredDy;
    }

    // Apply only the integer part that has not been applied yet and keep the fraction.
    // Adding the measurement directly (instead of filtering it) is what lets a slow
    // scroll be tracked exactly: filtering would lag behind and round the shift to zero.
    const int shift = int(std::lround(m_position - m_applied));
    m_applied += shift;
    step.dy = shift;
    step.valid = step.predicted ? shift != 0 : true;
    return step;
}
