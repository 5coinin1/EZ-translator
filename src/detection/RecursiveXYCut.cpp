#include "detection/RecursiveXYCut.h"

#include <algorithm>

namespace EZTranslator {
namespace {

struct Valley
{
    int  gap{0};
    int  position{0};
    bool valid{false};
};

int medianValue(QList<int> values)
{
    if (values.isEmpty())
        return 0;
    std::sort(values.begin(), values.end());
    return values.at(values.size() / 2);
}

QRect unionRect(const QList<QRect>& rects)
{
    QRect merged;
    for (const QRect& rect : rects)
        merged = merged.isNull() ? rect : merged.united(rect);
    return merged;
}

/** Khe dọc lớn nhất giữa các box (theo trục X). */
Valley widestValleyX(const QList<QRect>& rects)
{
    QList<QRect> sorted = rects;
    std::sort(sorted.begin(), sorted.end(),
              [](const QRect& a, const QRect& b) { return a.left() < b.left(); });

    Valley best;
    int maxRight = sorted.first().right();
    for (int i = 1; i < sorted.size(); ++i) {
        const int gap = sorted[i].left() - maxRight - 1;
        if (gap > best.gap) {
            best = {gap, (maxRight + sorted[i].left()) / 2, true};
        }
        maxRight = std::max(maxRight, sorted[i].right());
    }
    return best;
}

/** Khe ngang lớn nhất giữa các box (theo trục Y). */
Valley widestValleyY(const QList<QRect>& rects)
{
    QList<QRect> sorted = rects;
    std::sort(sorted.begin(), sorted.end(),
              [](const QRect& a, const QRect& b) { return a.top() < b.top(); });

    Valley best;
    int maxBottom = sorted.first().bottom();
    for (int i = 1; i < sorted.size(); ++i) {
        const int gap = sorted[i].top() - maxBottom - 1;
        if (gap > best.gap) {
            best = {gap, (maxBottom + sorted[i].top()) / 2, true};
        }
        maxBottom = std::max(maxBottom, sorted[i].bottom());
    }
    return best;
}

void recurse(const QList<QRect>& rects, int columnThreshold, int rowThreshold,
             int depth, int maxDepth, QList<QRect>& out)
{
    if (rects.isEmpty())
        return;
    if (rects.size() == 1 || depth >= maxDepth) {
        out.append(unionRect(rects));
        return;
    }

    const Valley valleyX = widestValleyX(rects);
    const Valley valleyY = widestValleyY(rects);

    const double cutX = (valleyX.valid && valleyX.gap >= columnThreshold)
                            ? double(valleyX.gap) / columnThreshold : 0.0;
    const double cutY = (valleyY.valid && valleyY.gap >= rowThreshold)
                            ? double(valleyY.gap) / rowThreshold : 0.0;

    if (cutX <= 0.0 && cutY <= 0.0) {
        out.append(unionRect(rects));
        return;
    }

    const bool vertical = cutX >= cutY;
    const int position = vertical ? valleyX.position : valleyY.position;

    QList<QRect> first;
    QList<QRect> second;
    for (const QRect& rect : rects) {
        const int center = vertical ? rect.center().x() : rect.center().y();
        (center < position ? first : second).append(rect);
    }

    if (first.isEmpty() || second.isEmpty()) {
        out.append(unionRect(rects));
        return;
    }

    recurse(first, columnThreshold, rowThreshold, depth + 1, maxDepth, out);
    recurse(second, columnThreshold, rowThreshold, depth + 1, maxDepth, out);
}

} // namespace

QList<QRect> RecursiveXYCut::cut(const QList<QRect>& rects, const XYCutOptions& options)
{
    QList<QRect> result;
    if (rects.isEmpty())
        return result;

    QList<int> heights;
    heights.reserve(rects.size());
    for (const QRect& rect : rects)
        heights.append(rect.height());

    const int dominantHeight = std::max(1, medianValue(heights));
    const int columnThreshold = std::max(options.minGapPixels,
                                         int(dominantHeight * options.columnGapFactor));
    const int rowThreshold = std::max(options.minGapPixels,
                                      int(dominantHeight * options.rowGapFactor));

    recurse(rects, columnThreshold, rowThreshold, 0, options.maxDepth, result);
    return result;
}

} // namespace EZTranslator
