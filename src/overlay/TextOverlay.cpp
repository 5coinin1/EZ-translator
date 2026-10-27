#include "overlay/TextOverlay.h"

#include "platform/PlatformUtils.h"

#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QResizeEvent>
#include <QRegion>
#include <QTextLayout>
#include <QTextOption>

#include <algorithm>
#include <cmath>

namespace EZTranslator {

namespace {

/**
 * @brief Break chữ thành dòng và đặt vị trí từng dòng với khoảng cách dòng cố định.
 *
 * Trả tổng chiều cao đã dùng (để vòng lặp co cỡ chữ biết có tràn hay không).
 * @param lineAdvance chiều cao mỗi dòng (pixel widget); <= 0 = theo chiều cao chữ.
 */
qreal layoutParagraph(QTextLayout& layout, const QString& text, int wrapWidth, int lineAdvance)
{
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    layout.setTextOption(option);

    layout.beginLayout();
    qreal y = 0.0;
    while (true) {
        QTextLine line = layout.createLine();
        if (!line.isValid())
            break;
        line.setLineWidth(qreal(std::max(8, wrapWidth)));
        line.setPosition(QPointF(0.0, y));
        y += std::max(qreal(lineAdvance), QFontMetricsF(layout.font()).height());
    }
    layout.endLayout();
    return y;
}

} // namespace

QList<OverlayBox> shiftOverlayBoxes(const QList<OverlayBox>& boxes, int dy,
                                    const QRect& regionBounds)
{
    QList<OverlayBox> shifted;
    shifted.reserve(boxes.size());
    const QRect retained = regionBounds.adjusted(0, -regionBounds.height(),
                                                0, regionBounds.height());
    for (OverlayBox box : boxes) {
        box.rect.translate(0, dy);
        if (!box.rect.intersects(retained))
            continue;
        for (QRect& cover : box.coverRects)
            cover.translate(0, dy);
        // A plate/backdrop belongs to the old frame, not the moved text.
        box.plate = {};
        box.plateRect = {};
        box.backdrop = {};
        box.backdropRect = {};
        shifted.append(std::move(box));
    }
    return shifted;
}

TextOverlay::TextOverlay(QWidget* parent)
    : QWidget(parent)
{
    m_autoFit = qEnvironmentVariable("EZ_OVERLAY_FIT", QStringLiteral("1")).trimmed()
                != QStringLiteral("0");
    m_glass = qEnvironmentVariable("EZ_OVERLAY_GLASS", QStringLiteral("1")).trimmed()
              != QStringLiteral("0");
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                   | Qt::WindowTransparentForInput | Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setFocusPolicy(Qt::NoFocus);
}

TextOverlay::~TextOverlay() = default;

void TextOverlay::showOverTarget(const QRect& clientLogicalRect)
{
    if (!clientLogicalRect.isValid() || clientLogicalRect.isEmpty()) {
        hideOverlay();
        return;
    }

    m_target = clientLogicalRect;
    if (geometry() != clientLogicalRect)
        setGeometry(clientLogicalRect);
    m_fitDirty = true;

    if (!isVisible())
        show();
    raise();
}

void TextOverlay::hideOverlay()
{
    m_boxes.clear();
    m_fitted.clear();
    m_fitDirty = true;
    m_frameSize = QSize();
    m_target = QRect();
    hide();
}

void TextOverlay::setBoxes(const QList<OverlayBox>& boxes, const QSize& frameSize)
{
    m_boxes = boxes;
    m_frameSize = frameSize;
    m_fitted.clear();
    m_fitDirty = true;
    update();
}

void TextOverlay::clear()
{
    m_boxes.clear();
    m_fitted.clear();
    m_fitDirty = true;
    update();
}

void TextOverlay::setCoverColor(const QColor& color)
{
    if (color.isValid())
        m_coverColor = color;
}

void TextOverlay::setTextStyle(const QColor& color, bool bold, int fontPixelSize, int alignment,
                               int lineSpacingFramePx)
{
    if (color.isValid())
        m_textColor = color;
    m_bold = bold;
    m_fixedPixelSize = std::max(0, fontPixelSize);
    m_alignment = std::clamp(alignment, 0, 2);
    m_lineSpacingPx = std::max(0, lineSpacingFramePx);
    m_fitDirty = true;
    update();
}

void TextOverlay::setContentOpacityPercent(int percent)
{
    m_contentOpacity = qBound(0.05, double(percent) / 100.0, 1.0);
    update();
}

void TextOverlay::setExcludeFromCapture(bool enable)
{
    m_excludeFromCapture = enable;
    // winId() buộc tạo HWND native ngay cả khi overlay chưa từng hiện.
    EZTranslator::Platform::setExcludedFromCapture(quintptr(winId()), enable);
}

void TextOverlay::setAutoFit(bool enable)
{
    if (m_autoFit == enable)
        return;
    m_autoFit = enable;
    m_fitDirty = true;
    update();
}

void TextOverlay::setGlassEnabled(bool enable)
{
    if (m_glass == enable)
        return;
    m_glass = enable;
    update();
}

void TextOverlay::resizeEvent(QResizeEvent* event)
{
    m_fitDirty = true;
    QWidget::resizeEvent(event);
}

void TextOverlay::paintEvent(QPaintEvent*)
{
    if (m_target.isEmpty() || m_frameSize.isEmpty())
        return;

    const double scaleX = double(width()) / double(m_frameSize.width());
    const double scaleY = double(height()) / double(m_frameSize.height());

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setOpacity(m_contentOpacity);

    // HAI LƯỢT: che (plate/màu đục) của MỖI box trước, rồi mới vẽ chữ của mỗi box.
    for (const OverlayBox& box : m_boxes) {
        if (!box.coverOnly && box.text.isEmpty())
            continue;

        if (m_glass && !box.plate.isNull())
            continue;

        const QRectF target(box.rect.x() * scaleX, box.rect.y() * scaleY,
                            box.rect.width() * scaleX, box.rect.height() * scaleY);
        if (target.width() < 2.0 || target.height() < 2.0)
            continue;

        const QColor cover = box.coverColor.isValid() ? box.coverColor : m_coverColor;
        if (!box.plate.isNull() && box.plateRect.isValid()) {
            const QRectF plateTarget(box.plateRect.x() * scaleX, box.plateRect.y() * scaleY,
                                     box.plateRect.width() * scaleX,
                                     box.plateRect.height() * scaleY);
            // The plate is a whole-region image. Painting it for every paragraph
            // erases neighbouring source text and makes a page-sized mask.
            QRegion sourceLines;
            for (const QRect& coverRect : box.coverRects) {
                sourceLines += QRectF(coverRect.x() * scaleX, coverRect.y() * scaleY,
                                      coverRect.width() * scaleX,
                                      coverRect.height() * scaleY).adjusted(-2, -1, 2, 1).toAlignedRect();
            }
            if (sourceLines.isEmpty())
                sourceLines = target.toAlignedRect();
            painter.save();
            painter.setClipRegion(sourceLines);
            painter.drawImage(plateTarget, box.plate);
            painter.restore();
        } else if (box.coverRects.isEmpty()) {
            painter.fillRect(target, cover);
        } else {
            const qreal previousOpacity = painter.opacity();
            if (box.fitToBox)
                painter.setOpacity(1.0);
            for (const QRect& coverRect : box.coverRects) {
                const QRectF coverTarget(coverRect.x() * scaleX, coverRect.y() * scaleY,
                                         coverRect.width() * scaleX,
                                         coverRect.height() * scaleY);
                painter.fillRect(coverTarget.adjusted(-2, -1, 2, 1), cover);
            }
            painter.setOpacity(previousOpacity);
        }
    }

    if (m_autoFit && m_fitDirty)
        rebuildFit();

    if (m_glass) {
        for (int boxIndex = 0; boxIndex < m_boxes.size(); ++boxIndex) {
            const OverlayBox& box = m_boxes.at(boxIndex);
            if (!box.coverOnly && box.text.isEmpty())
                continue;
            const QRectF target(box.rect.x() * scaleX, box.rect.y() * scaleY,
                                box.rect.width() * scaleX, box.rect.height() * scaleY);
            if (target.width() < 2.0 || target.height() < 2.0)
                continue;
            QRectF textRect = target;
            QColor color = box.textColor.isValid() ? box.textColor : m_textColor;
            if (m_autoFit && boxIndex < m_fitted.size() && m_fitted.at(boxIndex).valid) {
                textRect = m_fitted.at(boxIndex).clipRect;
                if (m_fitted.at(boxIndex).textColor.isValid())
                    color = m_fitted.at(boxIndex).textColor;
            }
            paintGlass(painter, box, textRect, color);
        }
    }

    for (int boxIndex = 0; boxIndex < m_boxes.size(); ++boxIndex) {
        const OverlayBox& box = m_boxes.at(boxIndex);
        if (!box.coverOnly && box.text.isEmpty())
            continue;

        const QRectF target(box.rect.x() * scaleX, box.rect.y() * scaleY,
                            box.rect.width() * scaleX, box.rect.height() * scaleY);
        if (target.width() < 2.0 || target.height() < 2.0)
            continue;

        if (m_autoFit && boxIndex < m_fitted.size() && m_fitted.at(boxIndex).valid) {
            const FittedBox& fitted = m_fitted.at(boxIndex);
            painter.save();
            painter.setClipRect(fitted.clipRect);
            painter.setFont(fitted.font);
            painter.setPen(fitted.textColor.isValid() ? fitted.textColor : m_textColor);
            for (const FittedLine& line : fitted.lines)
                painter.drawText(QPointF(line.x, line.baselineY), line.text);
            painter.restore();
            continue;
        }

        // Backup: giữ nguyên cách vẽ cũ nếu auto-fit tắt/lỗi.
        paintLegacy(painter, box, target, scaleY);
    }
}

void TextOverlay::rebuildFit()
{
    m_fitted.clear();
    if (!m_autoFit || m_boxes.isEmpty() || m_frameSize.isEmpty()
        || width() <= 0 || height() <= 0) {
        m_fitDirty = false;
        return;
    }

    const double scaleX = double(width()) / double(m_frameSize.width());
    const double scaleY = double(height()) / double(m_frameSize.height());

    // The median is only a fallback for boxes without measured source text.
    if (m_fixedPixelSize > 0) {
        m_fitBasePx = std::clamp(int(std::lround(m_fixedPixelSize * scaleY)), 9, 32);
    } else {
        QVector<int> sizes;
        sizes.reserve(m_boxes.size());
        for (const OverlayBox& box : m_boxes) {
            if (box.text.trimmed().isEmpty())
                continue;
            // UI: font ~ nua chieu cao dong. Novel: box cao bang ca paragraph
            // nen phai lay co chu tu dong nguon, khong lay tu rect.height.
            int size = box.fontPixelSize > 0
                           ? int(std::lround(box.fontPixelSize * scaleY))
                           : int(std::lround(box.rect.height() * scaleY * 0.5));
            if (size > 0)
                sizes.append(size);
        }
        if (sizes.isEmpty()) {
            m_fitBasePx = 0;
        } else {
            std::sort(sizes.begin(), sizes.end());
            m_fitBasePx = std::clamp(sizes.at(sizes.size() / 2), 9, 32);
        }
    }

    m_fitted.reserve(m_boxes.size());
    QVector<QRectF> targets;
    targets.reserve(m_boxes.size());
    for (const OverlayBox& box : m_boxes) {
        targets.append(QRectF(box.rect.x() * scaleX, box.rect.y() * scaleY,
                              box.rect.width() * scaleX, box.rect.height() * scaleY));
    }
    for (int i = 0; i < m_boxes.size(); ++i) {
        const OverlayBox& box = m_boxes.at(i);
        m_fitted.append(fitBox(box, targets.at(i), scaleX, scaleY, targets));
    }
    m_fitDirty = false;
}

TextOverlay::FittedBox TextOverlay::fitBox(const OverlayBox& box, const QRectF& target,
                                           double scaleX, double scaleY,
                                           const QVector<QRectF>& obstacles) const
{
    FittedBox result;
    if (box.text.trimmed().isEmpty() || target.width() < 2.0 || target.height() < 2.0)
        return result;

    // Vùng chữ gốc: ưu tiên union các line glyph (coverRects) thay vì paragraph
    // rect rộng hơn, để chữ dịch không tràn sang control bên cạnh.
    QRectF fitRect = target;
    if (!box.fitToBox && !box.coverRects.isEmpty()) {
        QRect unionRect;
        for (const QRect& rect : box.coverRects) {
            if (rect.width() <= 0 || rect.height() <= 0)
                continue;
            unionRect = unionRect.isNull() ? rect : unionRect.united(rect);
        }
        if (!unionRect.isNull()) {
            const QRectF mapped(unionRect.x() * scaleX, unionRect.y() * scaleY,
                                unionRect.width() * scaleX, unionRect.height() * scaleY);
            const QRectF clipped = mapped.intersected(target);
            if (clipped.width() >= 4.0 && clipped.height() >= 4.0)
                fitRect = clipped;
        }
    }
    fitRect.adjust(0.5, 0.5, -0.5, -0.5);
    if (fitRect.width() < 4.0 || fitRect.height() < 4.0)
        return result;

    // Mo rong ngang vao vung TRONG, nhung chi mot phan cua khoang trong den
    // OCR box ke tiep. Hai box canh nhau moi mo rong <45% gap nen khong de
    // chu cua box nay de len box kia (loi da thay tren event cards/tab bar).
    const QRectF plateBounds(0.0, 0.0, double(width()), double(height()));
    double gapLeft = 1e9;
    double gapRight = 1e9;
    for (const QRectF& obstacle : obstacles) {
        if (obstacle == target)
            continue;
        if (obstacle.bottom() <= fitRect.top() + 1.0
            || obstacle.top() >= fitRect.bottom() - 1.0)
            continue;
        if (obstacle.left() >= fitRect.right())
            gapRight = std::min(gapRight, obstacle.left() - fitRect.right());
        if (obstacle.right() <= fitRect.left())
            gapLeft = std::min(gapLeft, fitRect.left() - obstacle.right());
    }
    // Tam thoi khong mo rong: event cards/tab bar co nhieu control sat nhau,
    // mo rong lam text de len control ben canh. Uu tien khong tran.
    const double maxRight = 0.0;
    const double maxLeft = 0.0;
    const auto collides = [&](const QRectF& candidate) {
        for (const QRectF& obstacle : obstacles) {
            if (obstacle == target)
                continue;
            if (candidate.intersects(obstacle.adjusted(-1.0, -1.0, 1.0, 1.0)))
                return true;
        }
        return false;
    };
    for (double amount = 2.0; amount <= maxRight; amount += 2.0) {
        QRectF candidate = fitRect;
        candidate.setRight(fitRect.right() + amount);
        if (!plateBounds.contains(candidate) || collides(candidate))
            break;
        fitRect = candidate;
    }
    for (double amount = 2.0; amount <= maxLeft; amount += 2.0) {
        QRectF candidate = fitRect;
        candidate.setLeft(fitRect.left() - amount);
        if (!plateBounds.contains(candidate) || collides(candidate))
            break;
        fitRect = candidate;
    }

    const double availW = fitRect.width();
    const double availH = fitRect.height();

    QFont base = font();
    base.setBold(m_bold);

    // Mau chu theo NEN THAT dưới box (plate), không dùng một màu cho cả vùng:
    // label trên nền sáng cần chữ đậm, value trên thanh đỏ/đen cần chữ sáng.
    QColor fittedTextColor = box.textColor.isValid() ? box.textColor : m_textColor;
    if (!box.fitToBox && !box.plate.isNull() && box.plateRect.isValid()
        && !box.rect.isEmpty()) {
        const QRectF area = box.plateRect;
        if (area.width() > 0.0 && area.height() > 0.0) {
            int lumaSum = 0;
            int lumaCount = 0;
            const qreal fx[5] = {0.5, 0.3, 0.7, 0.5, 0.5};
            const qreal fy[5] = {0.5, 0.5, 0.5, 0.3, 0.7};
            for (int i = 0; i < 5; ++i) {
                const qreal frameX = box.rect.left() + box.rect.width() * fx[i];
                const qreal frameY = box.rect.top() + box.rect.height() * fy[i];
                const int px = int(std::lround((frameX - area.left()) / area.width()
                                               * box.plate.width()));
                const int py = int(std::lround((frameY - area.top()) / area.height()
                                               * box.plate.height()));
                if (px < 0 || py < 0 || px >= box.plate.width() || py >= box.plate.height())
                    continue;
                lumaSum += qGray(box.plate.pixel(px, py));
                ++lumaCount;
            }
            if (lumaCount > 0) {
                const int luma = lumaSum / lumaCount;
                fittedTextColor = luma >= 140 ? QColor(18, 22, 28) : QColor(248, 250, 252);
            }
        }
    }

    const int heightLimit = std::max(6, int(std::floor(availH)));
    int maxSize = heightLimit;
    if (m_fixedPixelSize > 0) {
        maxSize = std::min(maxSize, std::max(6, int(std::lround(m_fixedPixelSize * scaleY))));
    } else if (box.fontPixelSize > 0) {
        maxSize = std::min(maxSize, std::max(6, int(std::lround(box.fontPixelSize * scaleY))));
    } else if (m_fitBasePx > 0) {
        maxSize = std::min(maxSize, m_fitBasePx);
    } else {
        maxSize = std::min(maxSize, std::max(8, int(std::lround(availH * 0.7))));
    }
    maxSize = std::clamp(maxSize, 8, 64);

    // Khong de chu te o 6px: co nho nhat ~78% co chuan cua vung. Neu van
    // khong vua thi elide/ cat dong, giu duoc gu typography cua UI goc.
    const int minSize = std::max(6, std::min(maxSize,
        std::max(9, int(std::lround(maxSize * 0.72)))));

    // Box 1 dòng chỉ giữ 1 dòng nếu chiều cao không đủ cho 2 dòng. Label UI
    // tiếng Việt thường dài hơn tiếng Trung; cho wrap tối đa 2 dòng trong
    // đúng control thay vì bắt buộc elide gần hết chữ.
    const bool roomForTwoLines =
        fitRect.height() >= std::max(12.0, double(maxSize) * 1.9);
    const bool shortText = box.text.simplified().size() <= 6;
    const bool singleLine =
        box.coverRects.size() <= 1 && (shortText || !roomForTwoLines);
    if (singleLine) {
        QString text = box.text;
        text.replace(QLatin1Char('\n'), QLatin1Char(' '));
        text = text.simplified();
        if (text.isEmpty())
            return result;

        int lo = minSize;
        int hi = maxSize;
        int best = 0;
        while (lo <= hi) {
            const int middle = (lo + hi) / 2;
            QFont candidate = base;
            candidate.setPixelSize(middle);
            const QFontMetricsF metrics(candidate);
            if (metrics.horizontalAdvance(text) <= availW && metrics.height() <= availH) {
                best = middle;
                lo = middle + 1;
            } else {
                hi = middle - 1;
            }
        }

        QFont chosen = base;
        if (best >= minSize) {
            chosen.setPixelSize(best);
        } else {
            chosen.setPixelSize(minSize);
            QFontMetricsF metrics(chosen);
            const qreal needed = metrics.horizontalAdvance(text);
            if (needed > availW && needed > 0.0) {
                int stretch = int(std::floor(100.0 * availW / needed));
                stretch = std::clamp(stretch, 90, 100);
                chosen.setStretch(stretch);
                QFontMetricsF stretched(chosen);
                if (stretched.horizontalAdvance(text) > availW)
                    text = stretched.elidedText(text, Qt::ElideRight, availW);
            }
        }

        const QFontMetricsF metrics(chosen);
        const qreal textWidth = metrics.horizontalAdvance(text);
        qreal x = fitRect.left();
        if (m_alignment == 1)
            x += (availW - textWidth) / 2.0;
        else if (m_alignment == 2)
            x += availW - textWidth;

        result.font = chosen;
        result.textColor = fittedTextColor;
        result.clipRect = fitRect;
        result.lines.append(FittedLine{text, x,
                                       fitRect.top() + (availH - metrics.height()) / 2.0
                                           + metrics.ascent(),
                                       textWidth});
        result.valid = true;
        return result;
    }

    // Box nhiều dòng (đoạn văn/label dài): wrap trong đúng bounds, co cỡ chữ
    // theo chiều cao; nếu vẫn quá cao thì cắt bớt dòng cuối + ellipsis.
    const int lineAdvance = m_lineSpacingPx > 0
                                ? std::max(6, int(std::lround(m_lineSpacingPx * scaleY)))
                                : 0;

    const auto buildLines = [&](int pixelSize, QVector<FittedLine>* out) -> qreal {
        QFont candidate = base;
        candidate.setPixelSize(pixelSize);
        const QFontMetricsF lineMetrics(candidate);
        QTextLayout layout;
        layout.setText(box.text);
        layout.setFont(candidate);
        QTextOption option;
        option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        layout.setTextOption(option);

        layout.beginLayout();
        qreal y = 0.0;
        while (true) {
            QTextLine line = layout.createLine();
            if (!line.isValid())
                break;
            line.setLineWidth(std::max(8.0, availW));
            line.setPosition(QPointF(0.0, y));
            if (out) {
                const QString lineText = box.text.mid(line.textStart(), line.textLength());
                out->append(FittedLine{lineText, 0.0,
                                       fitRect.top() + y + line.ascent(),
                                       line.naturalTextWidth()});
            }
            y += std::max(qreal(lineAdvance), lineMetrics.height());
        }
        layout.endLayout();
        return y;
    };

    int bestSize = 0;
    QVector<FittedLine> bestLines;
    int lo = minSize;
    int hi = maxSize;
    while (lo <= hi) {
        const int middle = (lo + hi) / 2;
        QVector<FittedLine> candidateLines;
        const qreal height = buildLines(middle, &candidateLines);
        if (height <= availH) {
            bestSize = middle;
            bestLines = candidateLines;
            lo = middle + 1;
        } else {
            hi = middle - 1;
        }
    }

    QFont chosen = base;
    if (bestSize >= minSize) {
        chosen.setPixelSize(bestSize);
    } else {
        chosen.setPixelSize(minSize);
        bestLines.clear();
        const qreal height = buildLines(minSize, &bestLines);
        const QFontMetricsF metrics(chosen);
        while (!bestLines.isEmpty()
               && (bestLines.last().baselineY + metrics.descent() - fitRect.top()) > availH) {
            bestLines.removeLast();
        }
        if (bestLines.isEmpty())
            return result;
        if (height > availH) {
            FittedLine& last = bestLines.last();
            last.text = metrics.elidedText(last.text + QStringLiteral("…"), Qt::ElideRight,
                                           availW);
            last.width = metrics.horizontalAdvance(last.text);
        }
    }

    if (box.coverRects.size() <= 1 && !bestLines.isEmpty()) {
        const QFontMetricsF metrics(chosen);
        const qreal used = bestLines.last().baselineY + metrics.descent() - fitRect.top();
        const qreal shift = std::max(0.0, (availH - used) / 2.0);
        for (FittedLine& line : bestLines)
            line.baselineY += shift;
    }

    for (FittedLine& line : bestLines) {
        qreal x = fitRect.left();
        if (m_alignment == 1)
            x += (availW - line.width) / 2.0;
        else if (m_alignment == 2)
            x += availW - line.width;
        line.x = x;
    }

    result.font = chosen;
    result.textColor = fittedTextColor;
    result.clipRect = fitRect;
    result.lines = bestLines;
    result.valid = !result.lines.isEmpty();
    return result;
}

void TextOverlay::paintGlass(QPainter& painter, const OverlayBox& box, const QRectF& textRect,
                             const QColor& textColor) const
{
    if (m_frameSize.isEmpty() || box.rect.isEmpty())
        return;
    const double scaleX = double(width()) / double(m_frameSize.width());
    const double scaleY = double(height()) / double(m_frameSize.height());
    const QRectF boxRect(box.rect.x() * scaleX, box.rect.y() * scaleY,
                         box.rect.width() * scaleX, box.rect.height() * scaleY);
    // Panel phu TOAN BO box nguon (paragraph/line), khong chi vung text fit;
    // neu chi phu text fit thi cac dong goc con lai se lo ra ngoai.
    QRectF panel = boxRect.adjusted(-2.0, -2.0, 2.0, 2.0)
                       .intersected(QRectF(0.0, 0.0, double(width()), double(height())));
    if (panel.width() < 8.0 || panel.height() < 8.0)
        return;

    QImage under(panel.size().toSize(), QImage::Format_ARGB32_Premultiplied);
    under.fill(Qt::transparent);
    {
        QPainter layer(&under);
        if (!box.backdrop.isNull() && box.backdropRect.isValid() && !m_frameSize.isEmpty()) {
            const double sx = double(width()) / double(m_frameSize.width());
            const double sy = double(height()) / double(m_frameSize.height());
            const QRectF panelFrame(panel.left() / sx, panel.top() / sy,
                                    panel.width() / sx, panel.height() / sy);
            const QRectF srcFrame = panelFrame.intersected(box.backdropRect);
            if (srcFrame.width() >= 2.0 && srcFrame.height() >= 2.0) {
                const QRect src(
                    int(std::lround((srcFrame.left() - box.backdropRect.left())
                                    / box.backdropRect.width() * box.backdrop.width())),
                    int(std::lround((srcFrame.top() - box.backdropRect.top())
                                    / box.backdropRect.height() * box.backdrop.height())),
                    std::max(1, int(std::lround(srcFrame.width() / box.backdropRect.width()
                                                * box.backdrop.width()))),
                    std::max(1, int(std::lround(srcFrame.height() / box.backdropRect.height()
                                                * box.backdrop.height()))));
                QImage crop = box.backdrop.copy(src.intersected(box.backdrop.rect()));
                if (!crop.isNull()) {
                    const QSize small(std::max(1, crop.width() / 3),
                                      std::max(1, crop.height() / 3));
                    const QImage soft =
                        crop.scaled(small, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                            .scaled(panel.size().toSize(), Qt::IgnoreAspectRatio,
                                    Qt::SmoothTransformation);
                    layer.drawImage(panel, soft);
                }
            }
        }
        // Local plate (text already erased) keeps the original glyphs from
        // showing through the glass; draw it only inside the panel.
        if (!box.plate.isNull() && box.plateRect.isValid() && !m_frameSize.isEmpty()) {
            const double sx = double(width()) / double(m_frameSize.width());
            const double sy = double(height()) / double(m_frameSize.height());
            const QRectF panelFrame(panel.left() / sx, panel.top() / sy,
                                    panel.width() / sx, panel.height() / sy);
            const QRectF srcFrame = panelFrame.intersected(box.plateRect);
            if (srcFrame.width() >= 2.0 && srcFrame.height() >= 2.0) {
                const QRect src(
                    int(std::lround((srcFrame.left() - box.plateRect.left())
                                    / box.plateRect.width() * box.plate.width())),
                    int(std::lround((srcFrame.top() - box.plateRect.top())
                                    / box.plateRect.height() * box.plate.height())),
                    std::max(1, int(std::lround(srcFrame.width() / box.plateRect.width()
                                                * box.plate.width()))),
                    std::max(1, int(std::lround(srcFrame.height() / box.plateRect.height()
                                                * box.plate.height()))));
                const QRectF dest((srcFrame.left() - panelFrame.left()) / panelFrame.width()
                                      * panel.width(),
                                  (srcFrame.top() - panelFrame.top()) / panelFrame.height()
                                      * panel.height(),
                                  srcFrame.width() / panelFrame.width() * panel.width(),
                                  srcFrame.height() / panelFrame.height() * panel.height());
                layer.drawImage(dest, box.plate, src.intersected(box.plate.rect()));
            }
        }
    }

    // Very light tint; the texture/lines must stay visible.
    QImage tinted = under;
    {
        QPainter tint(&tinted);
        tint.setCompositionMode(QPainter::CompositionMode_SourceAtop);
        tint.fillRect(tinted.rect(), textColor.lightness() < 128
                                         ? QColor(255, 255, 255, 38)
                                         : QColor(0, 0, 0, 38));
    }

    // Soft feathered alpha: opaque under text, fades to zero at the edge,
    // no hard border. The rounded alpha is blurred so it blends with the UI.
    QImage mask(panel.size().toSize(), QImage::Format_ARGB32_Premultiplied);
    mask.fill(Qt::transparent);
    {
        QPainter maskPainter(&mask);
        maskPainter.setRenderHint(QPainter::Antialiasing, true);
        maskPainter.setPen(Qt::NoPen);
        maskPainter.setBrush(Qt::white);
        const qreal radius = std::min(12.0, panel.height() / 3.0);
        maskPainter.drawRoundedRect(QRectF(4.0, 4.0, panel.width() - 8.0,
                                           panel.height() - 8.0),
                                    radius, radius);
    }
    const QImage softMask =
        mask.scaled(std::max(1, int(panel.width() / 8.0)),
                    std::max(1, int(panel.height() / 8.0)),
                    Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
            .scaled(mask.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    {
        QPainter apply(&tinted);
        apply.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        apply.drawImage(0, 0, softMask);
    }
    painter.drawImage(panel.topLeft(), tinted);

    // A soft focus behind the glyphs: lower transparency exactly where text
    // sits, fading out before the panel edge; no border.
    QRectF focus = textRect.intersected(panel);
    if (focus.width() >= 2.0 && focus.height() >= 2.0) {
        QRadialGradient gradient(focus.center(),
                                 std::max(focus.width(), focus.height()) * 0.70);
        QColor core = textColor.lightness() < 128 ? QColor(255, 255, 255, 110)
                                                  : QColor(0, 0, 0, 120);
        QColor edge = core;
        edge.setAlpha(0);
        gradient.setColorAt(0.0, core);
        gradient.setColorAt(1.0, edge);
        painter.fillRect(panel, gradient);
    }
}

void TextOverlay::paintLegacy(QPainter& painter, const OverlayBox& box, const QRectF& target,
                              double scaleY) const
{
    int pixelSize = 0;
    if (m_fixedPixelSize > 0)
        pixelSize = int(std::lround(m_fixedPixelSize * scaleY));
    else if (box.fontPixelSize > 0)
        pixelSize = int(std::lround(box.fontPixelSize * scaleY));
    else
        pixelSize = int(target.height() * 0.5);
    pixelSize = std::max(6, pixelSize);

    QFont font = painter.font();
    font.setBold(m_bold);

    const int lineAdvance =
        m_lineSpacingPx > 0 ? std::max(6, int(std::lround(m_lineSpacingPx * scaleY))) : 0;
    const QRect textRect = target.adjusted(2, 1, -2, -1).toRect();

    QTextLayout layout;
    for (int attempt = 0;; ++attempt) {
        font.setPixelSize(pixelSize);
        layout.setText(box.text);
        layout.setFont(font);
        const qreal neededHeight =
            layoutParagraph(layout, box.text, textRect.width(), lineAdvance);
        if (neededHeight <= std::max(8, textRect.height()) || pixelSize <= 6 || attempt >= 24)
            break;
        pixelSize = std::max(6, int(pixelSize * 0.93));
    }

    painter.setFont(font);
    painter.setPen(box.textColor.isValid() ? box.textColor : m_textColor);

    for (int i = 0; i < layout.lineCount(); ++i) {
        const QTextLine line = layout.lineAt(i);
        qreal x = textRect.left();
        if (m_alignment == 1)
            x += (textRect.width() - line.naturalTextWidth()) / 2.0;
        else if (m_alignment == 2)
            x += textRect.width() - line.naturalTextWidth();
        const QString lineText = box.text.mid(line.textStart(), line.textLength());
        painter.drawText(QPointF(x, textRect.top() + line.y() + line.ascent()), lineText);
    }
}

} // namespace EZTranslator
