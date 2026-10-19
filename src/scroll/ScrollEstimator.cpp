#include "scroll/ScrollEstimator.h"

#include <QVarLengthArray>

#include <algorithm>
#include <cmath>

namespace {

// Luminance sample used by the profile. The green channel stands in for the grey level
// on 32 bit images: the profile is an alignment signature, not a colour measurement, and
// skipping qGray() keeps the per pixel cost down in the hottest loop of the tracker.
// Luminance sample for the profile. The green channel stands in for the grey level on
// 32 bit images: the profile is an alignment signature, not a colour measurement, and
// skipping qGray() keeps the per pixel cost down in the hottest loop of the tracker.
inline int grayAt(const QImage &image, int x, int y, bool thirtyTwoBit)
{
    const uchar *line = image.constScanLine(y);
    return thirtyTwoBit ? qGray(reinterpret_cast<const QRgb *>(line)[x])
                        : line[x * (image.depth() / 8)];
}

} // namespace

GrayProfile grayProfile(const QImage &image, const QRect &rect, int step, bool withGradients)
{
    GrayProfile profile;
    if (image.isNull())
        return profile;

    const QRect area = rect.intersected(image.rect());
    if (area.width() < 2 || area.height() < 2)
        return profile;

    if (step < 1)
        step = 1;

    const int width = area.width();
    const int height = area.height();
    const bool thirtyTwoBit = image.depth() == 32;
    const int top = area.top();
    const int left = area.left();
    const int lastRow = image.rect().bottom();
    const int lastCol = image.rect().right();

    profile.rows.resize(height);
    profile.cols.resize(width);
    if (withGradients)
        profile.edgeRows.resize(height);

    // Row pass. The gradient compares a sample with the same sample of the previous
    // row, which is still in a small rolling buffer, so one pixel read per sample feeds
    // both the luminance and the gradient signal (reading y-1 and y+1 separately would
    // cost three reads per sample).
    const int samples = (width + step - 1) / step;
    QList<int> previousRow(samples, -1);
    QList<int> currentRow(samples, 0);

    for (int y = 0; y < height; ++y) {
        const int sy = top + y;
        const bool hasPreviousRow = y > 0;

        double sum = 0.0;
        double edgeSum = 0.0;
        int index = 0;
        int count = 0;
        for (int x = 0; x < width; x += step, ++index) {
            const int sx = left + x;
            const int gray = grayAt(image, sx, sy, thirtyTwoBit);
            currentRow[index] = gray;
            sum += gray;
            if (withGradients) {
                if (hasPreviousRow) {
                    edgeSum += std::abs(gray - previousRow.at(index));
                } else if (sy < lastRow) {
                    // First row: only the row below is available.
                    edgeSum += std::abs(grayAt(image, sx, sy + 1, thirtyTwoBit) - gray);
                }
            }
            ++count;
        }
        profile.rows[y] = count > 0 ? sum / count : 0.0;
        if (withGradients) {
            profile.edgeRows[y] = count > 0 ? edgeSum / count : 0.0;
            currentRow.swap(previousRow);
        }
    }

    // Column pass: luminance only. A text page is aligned vertically, and the column
    // signal only matters for the rare horizontal component, so the gradient columns
    // are not worth a second set of reads.
    for (int x = 0; x < width; ++x) {
        const int sx = left + x;
        double sum = 0.0;
        int count = 0;
        for (int y = 0; y < height; y += step) {
            sum += grayAt(image, sx, top + y, thirtyTwoBit);
            ++count;
        }
        profile.cols[x] = count > 0 ? sum / count : 0.0;
    }

    return profile;
}

namespace {

// Normalized alignment score of `b` onto `a` when `b` is shifted by `s`. `prefixA` and
// `prefixB` hold the prefix sums of the (mean removed) signals, so the means cost O(1)
// and the remaining work is a single pass over the overlap.
struct Signal
{
    const QList<double> *values = nullptr;
    QList<double> prefix; // prefix sums of the values

    void build(const QList<double> &source)
    {
        values = &source;
        const int n = source.size();
        prefix.resize(n + 1);
        prefix[0] = 0.0;
        for (int i = 0; i < n; ++i)
            prefix[i + 1] = prefix[i] + source.at(i);
    }
};

double alignmentScore(const Signal &a, const Signal &b, int s, int minOverlap)
{
    const int n = std::min(a.values->size(), b.values->size());
    const int from = std::max(0, -s);
    const int to = std::min(n, n - s);
    const int overlap = to - from;
    if (overlap < minOverlap)
        return -2.0; // unusable

    // Mean of the overlap from the prefix sums. Raw pointers from here on: this is the
    // innermost loop of the tracker and QList::at() adds a bounds check per element.
    const double *prefixA = a.prefix.constData();
    const double *prefixB = b.prefix.constData();
    const double meanA = (prefixA[to] - prefixA[from]) / overlap;
    const double meanB = (prefixB[to + s] - prefixB[from + s]) / overlap;

    // A flat signal carries no structure to correlate (a blank area, or a page that
    // only varies along the other axis). The mean absolute difference is still
    // meaningful there: "flat vs varying" scores low, "flat vs flat" matches.
    const double *av = a.values->constData();
    const double *shifted = b.values->constData() + s;
    double dot = 0.0;
    double normA = 0.0;
    double normB = 0.0;
    double mad = 0.0;
    for (int i = from; i < to; ++i) {
        const double da = av[i] - meanA;
        const double db = shifted[i] - meanB;
        dot += da * db;
        normA += da * da;
        normB += db * db;
        mad += std::abs(av[i] - shifted[i]);
    }

    if (normA <= 1e-9 || normB <= 1e-9)
        return 1.0 - (mad / overlap) / 255.0;

    return 0.5 * (dot / std::sqrt(normA * normB) + 1.0);
}

// Best alignment of `b` onto `a`. Writes the winning integer shift, the interpolated
// (sub-pixel) shift and returns the score.
double bestShift(const QList<double> &a, const QList<double> &b, int maxShift, int *shiftOut,
                 double *exactOut, double preferredShift)
{
    if (a.isEmpty() || b.isEmpty()) {
        if (shiftOut)
            *shiftOut = 0;
        if (exactOut)
            *exactOut = 0.0;
        return -2.0;
    }

    const int n = std::min(a.size(), b.size());
    const int limit = maxShift > 0 ? std::min(maxShift, n / 2) : n / 3;
    const int minOverlap = std::max(6, n / 4);

    Signal sa;
    Signal sb;
    sa.build(a);
    sb.build(b);

    // Coarse to fine: a wide window is scanned with a stride, then only the best few
    // candidates are refined at full resolution. Searching every shift directly is
    // O(limit * n), which is the dominant cost of the whole tracking path.
    constexpr double kScoreSlack = 0.005;
    const int stride = limit > 24 ? 4 : 1;

    // One slot per shift: shifts are contiguous, so an array keyed by (s + limit) keeps
    // evaluation and selection trivial (-2 marks "not evaluated yet").
    constexpr double kUnused = -2.0;
    QVarLengthArray<double, 1024> scores(2 * limit + 1);
    std::fill(scores.begin(), scores.end(), kUnused);
    const auto scoreAt = [&](int s) -> double & { return scores[s + limit]; };
    const auto evaluate = [&](int s) {
        if (scoreAt(s) == kUnused)
            scoreAt(s) = alignmentScore(sa, sb, s, minOverlap);
    };

    for (int s = -limit; s <= limit; s += stride)
        evaluate(s);

    // Refine only around the coarse candidates that could still win.
    if (stride > 1) {
        double coarseBest = kUnused;
        for (int s = -limit; s <= limit; s += stride)
            coarseBest = std::max(coarseBest, scoreAt(s));
        for (int s = -limit; s <= limit; s += stride) {
            if (scoreAt(s) < coarseBest - kScoreSlack)
                continue;
            const int from = std::max(-limit, s - stride);
            const int to = std::min(limit, s + stride);
            for (int refined = from; refined <= to; ++refined)
                evaluate(refined);
        }
    }

    double bestScore = kUnused;
    int bestCandidate = -limit;
    for (int s = -limit; s <= limit; ++s) {
        if (scoreAt(s) > bestScore) {
            bestScore = scoreAt(s);
            bestCandidate = s;
        }
    }
    if (bestScore <= kUnused) {
        if (shiftOut)
            *shiftOut = 0;
        if (exactOut)
            *exactOut = 0.0;
        return 0.0;
    }

    // The best alignment wins, unless there is a FAR, equally good candidate: that is
    // the aliasing case (a page whose lines are evenly spaced correlates just as well a
    // whole line pitch away), and there the one closest to the expected shift is the
    // right answer. A shift only a pixel or two away is not an alias - it is just a
    // slightly worse alignment - so it must not be preferred over the best one.
    constexpr int kMinAliasSeparation = 6;
    int bestS = bestCandidate;
    double bestDistance = std::abs(double(bestS) - preferredShift);
    for (int s = -limit; s <= limit; ++s) {
        if (scoreAt(s) < bestScore - kScoreSlack)
            continue;
        // Compare against the absolute best peak, which stays fixed. Comparing against
        // an already moved `bestS` would let the chosen shift walk towards zero in
        // steps of the separation and stop short of the best alignment.
        if (std::abs(s - bestCandidate) < kMinAliasSeparation)
            continue;
        const double distance = std::abs(double(s) - preferredShift);
        if (distance < bestDistance) {
            bestDistance = distance;
            bestS = s;
        }
    }
    // The reported score is the winner's own score, so a caller threshold stays honest
    // even when an aliased candidate was preferred.
    bestScore = alignmentScore(sa, sb, bestS, minOverlap);

    // Parabolic refinement around the peak: with y(-1), y(0), y(+1) the true peak sits
    // delta = 0.5 * (y(-1) - y(+1)) / (y(-1) - 2y(0) + y(+1)) samples away.
    double exact = double(bestS);
    if (bestS > -limit && bestS < limit) {
        const double yPrev = alignmentScore(sa, sb, bestS - 1, minOverlap);
        const double yNext = alignmentScore(sa, sb, bestS + 1, minOverlap);
        const double denom = yPrev - 2.0 * bestScore + yNext;
        if (std::abs(denom) > 1e-9) {
            const double delta = 0.5 * (yPrev - yNext) / denom;
            if (std::abs(delta) <= 1.0)
                exact = bestS + delta;
        }
    }

    if (shiftOut)
        *shiftOut = int(std::lround(exact));
    if (exactOut)
        *exactOut = exact;

    return std::max(0.0, std::min(1.0, bestScore));
}

ScrollOffset matchSignal(const QList<double> &prevRows, const QList<double> &prevCols,
                         const QList<double> &curRows, const QList<double> &curCols, int maxShift,
                         double minScore, double preferredShift)
{
    ScrollOffset offset;
    if (prevRows.isEmpty() || curRows.isEmpty() || prevCols.isEmpty() || curCols.isEmpty())
        return offset;

    int dy = 0;
    int dx = 0;
    double dyExact = 0.0;
    double dxExact = 0.0;
    const double rowScore = bestShift(prevRows, curRows, maxShift, &dy, &dyExact, preferredShift);
    const double colScore = bestShift(prevCols, curCols, maxShift, &dx, &dxExact, 0.0);

    // Vertical scrolling is the main case; a text page's column profile is nearly
    // constant, so it must not be allowed to veto an otherwise perfect row match.
    // Horizontal motion is only trusted when the columns clearly agree.
    offset.dy = dy;
    offset.dyExact = dyExact;
    const bool trustCols = colScore >= 0.85 && colScore >= rowScore - 0.15;
    offset.dx = trustCols ? dx : 0;
    offset.dxExact = trustCols ? dxExact : 0.0;
    offset.score = rowScore;
    offset.valid = rowScore >= minScore;
    return offset;
}

} // namespace

ScrollOffset matchProfile(const GrayProfile &previous, const GrayProfile &current,
                          int maxShift, double minScore, double preferredShift)
{
    ScrollOffset offset;
    if (previous.isEmpty() || current.isEmpty())
        return offset;

    offset = matchSignal(previous.rows, previous.cols, current.rows, current.cols, maxShift,
                         minScore, preferredShift);

    // Luminance could not align the frames. Text is a texture of edges, so the gradient
    // signal usually can: this is what keeps low contrast pages and pages with a
    // background gradient trackable.
    if (!offset.valid && !previous.edgeRows.isEmpty() && !current.edgeRows.isEmpty()) {
        const ScrollOffset gradient = matchSignal(previous.edgeRows, previous.edgeCols,
                                                  current.edgeRows, current.edgeCols, maxShift,
                                                  minScore, preferredShift);
        if (gradient.score > offset.score) {
            offset = gradient;
            offset.usedGradient = true;
        }
    }

    return offset;
}
