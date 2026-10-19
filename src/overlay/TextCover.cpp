#include "overlay/TextCover.h"

#include <QHash>
#include <QQueue>
#include <QVector>

#include <algorithm>
#include <cmath>

namespace {

// The erase is a port of the user's previous app (RSTGameTranslation, src/TextEraser.cs): an
// adaptive stroke threshold, stroke growth into the patch margin, an outside-in inpaint of the
// masked pixels and a soft alpha fade at the patch border, so the covered area keeps looking like
// the UI underneath instead of a flat box.
constexpr double kMaxInkDistance = 60.0;   // faint text (hints, greyed-out options) still counts
constexpr double kMinInkDistance = 25.0;   // below this, compression noise would count as text
constexpr double kInkContrastShare = 0.45; // share of the strongest contrast = stroke threshold
constexpr int kMaskDilation = 2;           // grow strokes to cover their anti-aliased edge
constexpr int kMarginStrokeGap = 2;        // largest gap still followed out of the box
constexpr double kMaxInkShare = 0.75;      // strokes above this share: refill the whole box
constexpr int kSmoothingPasses = 3;

// One text box being erased: the pixels of its patch (the box grown by a margin) plus the mask of
// the stroke pixels. `origin` is the patch position in the crop; left/top/right/bottom are the box
// inside the patch, patch-local.
struct Patch
{
    int originX = 0;
    int originY = 0;
    int w = 0;
    int h = 0;
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    QVector<float> r;
    QVector<float> g;
    QVector<float> b;
    QVector<bool> mask;
};

// Background of a box: the colour just above it and just below it, interpolated per row. A flat
// panel gives one colour, a gradient keeps its slope - one sampled colour (the old app's approach)
// cannot do that, and a box spanning more than the ink threshold of a strong gradient would have
// its far half mistaken for ink and flattened.
struct Background
{
    QColor top;
    QColor bottom;
    int boxTop = 0;
    int boxBottom = 0;

    QColor at(int row) const
    {
        if (!top.isValid())
            return bottom;
        if (!bottom.isValid() || row <= boxTop || boxBottom <= boxTop)
            return top;
        if (row >= boxBottom)
            return bottom;
        const double t = double(row - boxTop) / double(boxBottom - boxTop);
        return QColor(int(std::lround(top.red() + (bottom.red() - top.red()) * t)),
                      int(std::lround(top.green() + (bottom.green() - top.green()) * t)),
                      int(std::lround(top.blue() + (bottom.blue() - top.blue()) * t)));
    }
};

double colorDistance(float r, float g, float b, const QColor &color)
{
    const double dr = r - color.red();
    const double dg = g - color.green();
    const double db = b - color.blue();
    return std::sqrt(dr * dr + dg * dg + db * db);
}

// Most common colour (coarsely binned), refined as the average of all pixels close to it. This is
// the old app's ColorUtils.DominantColor: a ring around a gradient box is bimodal, and the mode
// (with a stable first-seen tie-break) picks the ring side the box belongs to.
QColor dominantColor(const QVector<QRgb> &pixels)
{
    if (pixels.isEmpty())
        return QColor();

    QHash<qint32, int> counts;
    QVector<qint32> order;
    const auto binOf = [](QRgb pixel) {
        return (((qint32(qRed(pixel)) >> 4) << 8) | (qint32(qGreen(pixel)) >> 4)) << 4
               | (qint32(qBlue(pixel)) >> 4);
    };
    for (const QRgb pixel : pixels) {
        const qint32 key = binOf(pixel);
        if (!counts.contains(key))
            order.append(key);
        ++counts[key];
    }
    qint32 bestKey = order.first();
    int bestCount = counts.value(bestKey);
    for (const qint32 key : order) {
        if (counts.value(key) > bestCount) {
            bestCount = counts.value(key);
            bestKey = key;
        }
    }

    double sr = 0;
    double sg = 0;
    double sb = 0;
    int n = 0;
    for (const QRgb pixel : pixels) {
        if (binOf(pixel) != bestKey)
            continue;
        sr += qRed(pixel);
        sg += qGreen(pixel);
        sb += qBlue(pixel);
        ++n;
    }
    const QColor seed(int(sr / n), int(sg / n), int(sb / n));

    // Refine: the average of every pixel within SameColorDistance (40) of the seed.
    constexpr double kSameColorDistance = 40.0;
    sr = sg = sb = 0;
    n = 0;
    for (const QRgb pixel : pixels) {
        const double dr = qRed(pixel) - seed.red();
        const double dg = qGreen(pixel) - seed.green();
        const double db = qBlue(pixel) - seed.blue();
        if (std::sqrt(dr * dr + dg * dg + db * db) > kSameColorDistance)
            continue;
        sr += qRed(pixel);
        sg += qGreen(pixel);
        sb += qBlue(pixel);
        ++n;
    }
    return n > 0 ? QColor(int(sr / n), int(sg / n), int(sb / n)) : seed;
}

// Dominant colour of the strip of `rows` pixels just above or just below the box.
QColor stripColor(const QImage &crop, const QRect &box, int rows, bool above)
{
    QVector<QRgb> pixels;
    for (int i = 1; i <= rows; ++i) {
        const int y = above ? box.top() - i : box.bottom() + i;
        if (y < 0 || y >= crop.height())
            continue;
        for (int x = box.left(); x <= box.right(); ++x)
            pixels.append(crop.pixel(x, y));
    }
    return dominantColor(pixels);
}

// Fallback when the box has no strips at all (it touches the crop edge): the dominant colour of
// the patch ring, then of the box itself (the old app's ColorUtils.SampleOverlayColors).
QColor ringColor(const QImage &crop, const QRect &box, const QRect &patch)
{
    QVector<QRgb> ring;
    QVector<QRgb> inside;
    for (int y = patch.top(); y <= patch.bottom(); ++y)
        for (int x = patch.left(); x <= patch.right(); ++x) {
            const QRgb pixel = crop.pixel(x, y);
            if (box.contains(x, y))
                inside.append(pixel);
            else
                ring.append(pixel);
        }
    return dominantColor(ring.isEmpty() ? inside : ring);
}

bool isInk(float r, float g, float b, const QColor &bg, double inkDistance)
{
    const double dr = r - bg.red();
    const double dg = g - bg.green();
    const double db = b - bg.blue();
    return dr * dr + dg * dg + db * db > inkDistance * inkDistance;
}

bool isCloserToInk(float r, float g, float b, const QColor &ink, const QColor &bg)
{
    const double ir = r - ink.red();
    const double ig = g - ink.green();
    const double ib = b - ink.blue();
    const double br = r - bg.red();
    const double bgg = g - bg.green();
    const double bb = b - bg.blue();
    return ir * ir + ig * ig + ib * ib < br * br + bgg * bgg + bb * bb;
}

// Stroke threshold for this box: a share of its strongest contrast (ignoring the top 2% as
// outliers), within [MinInkDistance, MaxInkDistance].
double estimateInkDistance(const Patch &p, const Background &bg)
{
    QVector<double> distances;
    distances.reserve((p.right - p.left) * (p.bottom - p.top));
    for (int y = p.top; y < p.bottom; ++y) {
        const QColor row = bg.at(y);
        for (int x = p.left; x < p.right; ++x) {
            const int i = y * p.w + x;
            distances.append(colorDistance(p.r[i], p.g[i], p.b[i], row));
        }
    }
    if (distances.isEmpty())
        return kMaxInkDistance;

    // Percentile 98: chỉ cần ĐÚNG phần tử ở vị trí đó — nth_element (O(n)) thay vì sort cả dải
    // (O(n log n)); cùng giá trị, nhanh hơn hẳn trên box lớn (plate là phần tốn nhất luồng UI).
    const int index = int((distances.size() - 1) * 0.98);
    std::nth_element(distances.begin(), distances.begin() + index, distances.end());
    const double strongest = distances.at(index);
    return std::clamp(strongest * kInkContrastShare, kMinInkDistance, kMaxInkDistance);
}

// Text strokes inside the box, dilated; the whole box when strokes dominate it.
void buildMask(Patch &p, const Background &bg, double inkDistance)
{
    const int count = p.w * p.h;
    QVector<bool> ink(count, false);
    int inkCount = 0;
    for (int y = p.top; y < p.bottom; ++y) {
        const QColor row = bg.at(y);
        for (int x = p.left; x < p.right; ++x) {
            const int i = y * p.w + x;
            if (isInk(p.r[i], p.g[i], p.b[i], row, inkDistance)) {
                ink[i] = true;
                ++inkCount;
            }
        }
    }

    p.mask = QVector<bool>(count, false);
    const bool wholeBox = inkCount > (p.right - p.left) * (p.bottom - p.top) * kMaxInkShare;
    if (wholeBox) {
        for (int y = p.top; y < p.bottom; ++y)
            for (int x = p.left; x < p.right; ++x)
                p.mask[y * p.w + x] = true;
        return;
    }

    // Giãn nở mask bằng cửa sổ vuông (2k+1)² TÁCH ĐƯỢC: max theo HÀNG rồi max theo CỘT cho kết
    // quả Y HỆT bản quét 25 ô lồng nhau, nhưng chi phí ~O(area) thay vì O(area×25) — đây là phần
    // tốn nhất của plate (log thật: dựng plate 97-214ms mỗi lần cuộn, chặn luôn lúc HIỆN overlay).
    QVector<bool> rowHit(count, false);
    for (int y = p.top; y < p.bottom; ++y)
        for (int x = p.left; x < p.right; ++x) {
            const int i = y * p.w + x;
            bool hit = false;
            for (int dx = -kMaskDilation; dx <= kMaskDilation && !hit; ++dx) {
                const int nx = x + dx;
                if (nx >= p.left && nx < p.right && ink[i + dx])
                    hit = true;
            }
            rowHit[i] = hit;
        }
    for (int y = p.top; y < p.bottom; ++y)
        for (int x = p.left; x < p.right; ++x) {
            const int i = y * p.w + x;
            bool hit = false;
            for (int dy = -kMaskDilation; dy <= kMaskDilation && !hit; ++dy) {
                const int ny = y + dy;
                if (ny >= p.top && ny < p.bottom && rowHit[i + dy * p.w])
                    hit = true;
            }
            p.mask[i] = hit;
        }
}

// Most common stroke colour inside the box (coarse bins). Returns false when there are no strokes.
bool dominantInkColor(const Patch &p, const Background &bg, double inkDistance, QColor *out)
{
    struct Bin
    {
        double r = 0;
        double g = 0;
        double b = 0;
        int count = 0;
    };
    QHash<qint32, Bin> bins;
    for (int y = p.top; y < p.bottom; ++y) {
        const QColor row = bg.at(y);
        for (int x = p.left; x < p.right; ++x) {
            const int i = y * p.w + x;
            if (!isInk(p.r[i], p.g[i], p.b[i], row, inkDistance))
                continue;
            const qint32 key = (((qint32(p.r[i]) >> 4) << 8 | (qint32(p.g[i]) >> 4)) << 4)
                               | (qint32(p.b[i]) >> 4);
            Bin &bin = bins[key];
            bin.r += p.r[i];
            bin.g += p.g[i];
            bin.b += p.b[i];
            ++bin.count;
        }
    }
    if (bins.isEmpty())
        return false;

    const Bin *best = nullptr;
    for (auto it = bins.constBegin(); it != bins.constEnd(); ++it) {
        if (!best || it.value().count > best->count)
            best = &it.value();
    }
    *out = QColor(int(std::lround(best->r / best->count)),
                  int(std::lround(best->g / best->count)),
                  int(std::lround(best->b / best->count)));
    return true;
}

// Follow strokes from inside the text box out into the margin (8-connected, through pixels that
// look like the ink) and mask them, dilated like the strokes inside. OCR boxes are tight and cut
// glyph parts off (descenders, accents, the edge of the last letter); those parts sit in the
// margin and stayed visible next to the translation without this.
void growMaskIntoMargin(Patch &p, const Background &bg, double inkDistance, const QColor &inkColor)
{
    const int count = p.w * p.h;
    QVector<bool> grown(count, false);
    QQueue<int> queue;
    const auto isStroke = [&](int i, int y) {
        const QColor row = bg.at(y);
        return isInk(p.r[i], p.g[i], p.b[i], row, inkDistance)
               && isCloserToInk(p.r[i], p.g[i], p.b[i], inkColor, row);
    };
    const auto inBox = [&](int x, int y) {
        return x >= p.left && x < p.right && y >= p.top && y < p.bottom;
    };

    for (int y = p.top; y < p.bottom; ++y)
        for (int x = p.left; x < p.right; ++x) {
            const int i = y * p.w + x;
            if (isStroke(i, y))
                queue.enqueue(i);
        }

    while (!queue.isEmpty()) {
        const int i = queue.dequeue();
        const int x = i % p.w;
        const int y = i / p.w;
        // Steps of up to MarginStrokeGap + 1 pixels also reach marks detached from their letter
        // (Vietnamese/French accents, the dot of i/j) without crossing the gap to the next line.
        for (int dy = -kMarginStrokeGap - 1; dy <= kMarginStrokeGap + 1; ++dy)
            for (int dx = -kMarginStrokeGap - 1; dx <= kMarginStrokeGap + 1; ++dx) {
                const int nx = x + dx;
                const int ny = y + dy;
                if (nx < 0 || ny < 0 || nx >= p.w || ny >= p.h || inBox(nx, ny))
                    continue;
                const int j = ny * p.w + nx;
                if (!grown[j] && isStroke(j, ny)) {
                    grown[j] = true;
                    queue.enqueue(j);
                }
            }
    }

    for (int y = 0; y < p.h; ++y)
        for (int x = 0; x < p.w; ++x) {
            if (!grown[y * p.w + x])
                continue;
            for (int dy = -kMaskDilation; dy <= kMaskDilation; ++dy)
                for (int dx = -kMaskDilation; dx <= kMaskDilation; ++dx) {
                    const int nx = x + dx;
                    const int ny = y + dy;
                    if (nx >= 0 && ny >= 0 && nx < p.w && ny < p.h)
                        p.mask[ny * p.w + nx] = true;
                }
        }
}

// Fill masked pixels from the outside in (each one averages its already known neighbours), then
// smooth the filled area so the fill order does not leave streaks.
void fillPatch(Patch &p, const QVector<bool> &usable, const Background &bg)
{
    const int count = p.w * p.h;
    QVector<bool> known = usable;
    QVector<bool> queued(count, false);
    QQueue<int> queue;
    const auto hasKnownNeighbour = [&](int i) {
        const int x = i % p.w;
        const int y = i / p.w;
        return (x > 0 && known[i - 1]) || (x < p.w - 1 && known[i + 1]) || (y > 0 && known[i - p.w])
               || (y < p.h - 1 && known[i + p.w]);
    };
    for (int i = 0; i < count; ++i)
        if (p.mask[i] && hasKnownNeighbour(i)) {
            queue.enqueue(i);
            queued[i] = true;
        }

    while (!queue.isEmpty()) {
        const int i = queue.dequeue();
        const int x = i % p.w;
        const int y = i / p.w;
        float sr = 0;
        float sg = 0;
        float sb = 0;
        int n = 0;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                const int nx = x + dx;
                const int ny = y + dy;
                if ((dx == 0 && dy == 0) || nx < 0 || ny < 0 || nx >= p.w || ny >= p.h)
                    continue;
                const int j = ny * p.w + nx;
                if (known[j]) {
                    sr += p.r[j];
                    sg += p.g[j];
                    sb += p.b[j];
                    ++n;
                } else if (p.mask[j] && !queued[j]) {
                    queue.enqueue(j);
                    queued[j] = true;
                }
            }
        if (n > 0) {
            p.r[i] = sr / n;
            p.g[i] = sg / n;
            p.b[i] = sb / n;
        }
        known[i] = true;
    }

    for (int pass = 0; pass < kSmoothingPasses; ++pass) {
        for (int i = 0; i < count; ++i) {
            if (!p.mask[i])
                continue;
            const int x = i % p.w;
            const int y = i / p.w;
            float sr = p.r[i];
            float sg = p.g[i];
            float sb = p.b[i];
            int n = 1;
            const auto add = [&](int j) {
                if (!p.mask[j] && !usable[j])
                    return;
                sr += p.r[j];
                sg += p.g[j];
                sb += p.b[j];
                ++n;
            };
            if (x > 0)
                add(i - 1);
            if (x < p.w - 1)
                add(i + 1);
            if (y > 0)
                add(i - p.w);
            if (y < p.h - 1)
                add(i + p.w);
            p.r[i] = sr / n;
            p.g[i] = sg / n;
            p.b[i] = sb / n;
        }
    }
}

// Builds the patch of one box inside `crop` and erases the strokes in it. Returns false when the
// box is unusable (empty intersection).
bool eraseBox(const QImage &crop, const QRect &rawBox, QImage *result)
{
    const QRect imageRect = crop.rect();
    const int margin = std::clamp(std::min(rawBox.width(), rawBox.height()) / 4, 3, 12);
    const QRect patchBounds =
        rawBox.adjusted(-margin, -margin, margin, margin).intersected(imageRect);
    const QRect inner = rawBox.intersected(imageRect);
    if (inner.width() <= 0 || inner.height() <= 0 || patchBounds.isEmpty())
        return false;

    Patch p;
    p.originX = patchBounds.x();
    p.originY = patchBounds.y();
    p.w = patchBounds.width();
    p.h = patchBounds.height();
    p.left = inner.x() - patchBounds.x();
    p.top = inner.y() - patchBounds.y();
    p.right = p.left + inner.width();
    p.bottom = p.top + inner.height();

    const int count = p.w * p.h;
    p.r.resize(count);
    p.g.resize(count);
    p.b.resize(count);
    for (int y = 0; y < p.h; ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(crop.constScanLine(p.originY + y));
        for (int x = 0; x < p.w; ++x) {
            const int i = y * p.w + x;
            const QRgb pixel = row[p.originX + x];
            p.r[i] = float(qRed(pixel));
            p.g[i] = float(qGreen(pixel));
            p.b[i] = float(qBlue(pixel));
        }
    }

    Background bg;
    bg.top = stripColor(crop, inner, 2, true);
    bg.bottom = stripColor(crop, inner, 2, false);
    if (!bg.top.isValid())
        bg.top = bg.bottom;
    if (!bg.bottom.isValid())
        bg.bottom = bg.top;
    if (!bg.top.isValid())
        bg.top = bg.bottom = ringColor(crop, inner, patchBounds);
    bg.boxTop = p.top;
    bg.boxBottom = p.bottom;

    // NỀN CỤC BỘ: dải trên/dưới chỉ đúng khi box nằm trên CÙNG một nền. Với UI nhiều khối nhỏ
    // (box trên panel/toggle khác màu), dải sai ⇒ mọi pixel của panel bị coi là "mực" ⇒ plate xoá
    // cả mảng UI thành khối đen (ca image7/WeGame). Lấy MÀU CHIẾM ƯU THẾ trong chính box làm nền
    // khi nó khác dải quá xa — chữ chỉ là thiểu số trong box nên ưu thế = nền thật.
    {
        QVector<QRgb> insidePixels;
        insidePixels.reserve(inner.width() * inner.height());
        for (int y = inner.top(); y <= inner.bottom(); ++y)
            for (int x = inner.left(); x <= inner.right(); ++x)
                insidePixels.append(crop.pixel(x, y));
        const QColor boxBg = dominantColor(insidePixels);
        const auto farFrom = [&boxBg](const QColor& c) {
            return c.isValid()
                   && (qAbs(c.red() - boxBg.red()) + qAbs(c.green() - boxBg.green())
                       + qAbs(c.blue() - boxBg.blue()))
                          > 120;
        };
        if (boxBg.isValid() && (farFrom(bg.top) || farFrom(bg.bottom)))
            bg.top = bg.bottom = boxBg;
    }

    const double inkDistance = estimateInkDistance(p, bg);
    buildMask(p, bg, inkDistance);
    QColor inkColor;
    const bool haveInk = dominantInkColor(p, bg, inkDistance, &inkColor);
    if (haveInk)
        growMaskIntoMargin(p, bg, inkDistance, inkColor);

    // Other strokes in the margin (neighbouring text, bubble outlines) stay as they are but must
    // not be smeared into the fill. Judge them by closeness to the ink colour rather than distance
    // from one background colour, which would reject half a gradient.
    QVector<bool> usable(count);
    for (int i = 0; i < count; ++i) {
        const QColor row = bg.at(i / p.w);
        usable[i] = !p.mask[i]
                    && (!haveInk || !isCloserToInk(p.r[i], p.g[i], p.b[i], inkColor, row));
    }

    // Start from the local background colour, so pixels no usable neighbour reaches still look
    // right (the old app seeds the fill the same way).
    for (int i = 0; i < count; ++i)
        if (p.mask[i]) {
            const QColor row = bg.at(i / p.w);
            p.r[i] = row.red();
            p.g[i] = row.green();
            p.b[i] = row.blue();
        }

    fillPatch(p, usable, bg);

    for (int y = 0; y < p.h; ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(result->scanLine(p.originY + y));
        for (int x = 0; x < p.w; ++x) {
            const int i = y * p.w + x;
            row[p.originX + x] = qRgb(std::clamp(int(std::lround(p.r[i])), 0, 255),
                                      std::clamp(int(std::lround(p.g[i])), 0, 255),
                                      std::clamp(int(std::lround(p.b[i])), 0, 255));
        }
    }
    return true;
}

} // namespace

QImage TextCover::removeText(const QImage &crop, const QList<QRect> &boxes)
{
    if (crop.isNull())
        return QImage();

    QImage result = crop.convertToFormat(QImage::Format_RGB32);
    if (boxes.isEmpty())
        return result;

    for (const QRect &box : boxes)
        eraseBox(result, box, &result);

    return result;
}

QImage TextCover::cleanPlate(const QImage &region, const QRect &area, const QList<QRect> &boxes)
{
    const QRect clip = area.intersected(region.rect());
    if (clip.isEmpty())
        return QImage();

    QImage erased = region.copy(clip).convertToFormat(QImage::Format_RGB32);
    QList<QRect> local;
    local.reserve(boxes.size());
    for (const QRect &box : boxes) {
        const QRect shifted = box.translated(-clip.topLeft());
        if (shifted.intersects(erased.rect()))
            local.append(shifted);
    }

    for (const QRect &box : local)
        eraseBox(erased, box, &erased);

    // Alpha, exactly like the old app's ToBitmapSource: opaque over the text boxes and over the
    // erased strokes in their margin, fading to transparent across the rest of the margin - so the
    // patch blends into the live screen at its border instead of ending in a hard rectangle.
    const int w = erased.width();
    const int h = erased.height();
    QVector<uchar> alpha(w * h, 0);
    for (const QRect &box : local) {
        const int margin = std::clamp(std::min(box.width(), box.height()) / 4, 3, 12);
        const QRect patch = box.adjusted(-margin, -margin, margin, margin).intersected(erased.rect());
        if (patch.isEmpty())
            continue;
        // Fade distance = the patch margin (the old app measures it on the patch, not on the whole
        // plate): the alpha reaches ~0 exactly at the patch border.
        const int boxLeft = box.left() - patch.left();
        const int boxTop = box.top() - patch.top();
        const int boxRight = box.left() + box.width() - patch.left();
        const int boxBottom = box.top() + box.height() - patch.top();
        const int fade = std::max(std::max(boxLeft, boxTop),
                                  std::max(patch.width() - boxRight, patch.height() - boxBottom));
        for (int y = patch.top(); y <= patch.bottom(); ++y)
            for (int x = patch.left(); x <= patch.right(); ++x) {
                const int localX = x - patch.left();
                const int localY = y - patch.top();
                const int dx = std::max(0, std::max(boxLeft - localX, localX - (boxRight - 1)));
                const int dy = std::max(0, std::max(boxTop - localY, localY - (boxBottom - 1)));
                const int outside = std::max(dx, dy);
                const double a = (outside == 0 || fade == 0)
                                     ? 1.0
                                     : std::max(0.0, 1.0 - double(outside) / (fade + 1));
                const uchar value = uchar(std::lround(a * 255.0));
                uchar &slot = alpha[y * w + x];
                slot = std::max(slot, value);
            }
    }

    QImage plate(erased.size(), QImage::Format_ARGB32_Premultiplied);
    plate.fill(Qt::transparent);
    for (int y = 0; y < h; ++y) {
        const QRgb *source = reinterpret_cast<const QRgb *>(erased.constScanLine(y));
        QRgb *output = reinterpret_cast<QRgb *>(plate.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const uchar a = alpha[y * w + x];
            if (a == 0) {
                output[x] = 0;
                continue;
            }
            const int r = qRed(source[x]) * a / 255;
            const int g = qGreen(source[x]) * a / 255;
            const int b = qBlue(source[x]) * a / 255;
            output[x] = qRgba(r, g, b, a);
        }
    }
    return plate;
}
