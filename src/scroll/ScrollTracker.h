#pragma once

// Tracks the vertical scroll of one region across frames.
//
// The matcher produces a noisy relative shift per frame. This keeps an absolute
// position + velocity estimate (an alpha-beta filter, which is the steady state form
// of a constant velocity Kalman filter) so that:
//   - the applied shift is smoothed instead of jumping with every measurement,
//   - the fractional part of the offset is carried over, so a slow scroll does not
//     round to 0 every frame and lose the accumulated movement,
//   - a frame the matcher could not align can still be followed by prediction (a fast
//     scroll is exactly when the matcher is most likely to give up),
//   - a page jump resets the model instead of dragging stale state along.
class ScrollTracker
{
public:
    struct Step
    {
        // Integer shift to apply for this frame. The tracker keeps the fractional part
        // internally, so a slow scroll does not lose its accumulated movement.
        int dy = 0;
        bool valid = false;     // false => nothing usable, the caller must not reuse
        bool predicted = false; // came from the velocity model, not from a measurement
    };

    void reset();
    bool hasState() const { return m_inited; }
    double velocity() const { return m_velocity; }

    // Relative shift the motion model expects for the next frame. Used as the prior
    // when disambiguating equal correlation peaks (regularly spaced text lines alias
    // badly) and to narrow the search window.
    double predictedStep() const { return m_inited ? m_velocity : 0.0; }

    // `measuredDy` is the relative shift the matcher reported, `accepted` says whether
    // the caller trusts it. `jumpPx` is the residual beyond which the motion is
    // considered a page jump (a new page, a paragraph replace) rather than scrolling.
    Step update(double measuredDy, bool accepted, double jumpPx);

private:
    double m_position = 0.0;     // absolute offset of the content, in pixels
    double m_applied = 0.0;      // integer part already applied to the boxes
    double m_velocity = 0.0;     // pixels per frame
    int m_misses = 0;
    bool m_inited = false;
};
