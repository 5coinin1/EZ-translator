#include "RoiGraphicsItem.h"
#include "ui/theme/StyleTheme.h"

#include <QPainter>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSceneContextMenuEvent>
#include <QMenu>
#include <QStyleOptionGraphicsItem>
#include <QCursor>
#include <QUuid>
#include <QFont>

RoiGraphicsItem::RoiGraphicsItem(const EZTranslator::TranslationRegion& region,
                                  QGraphicsItem* parent)
    : QGraphicsObject(parent)
    , m_region(region)
    , m_rect(0, 0, 200, 80)
{
    setFlags(ItemIsSelectable | ItemIsMovable | ItemSendsGeometryChanges);
    setAcceptHoverEvents(true);
}

QRectF RoiGraphicsItem::boundingRect() const
{
    return m_rect.adjusted(-HandleSize, -HandleSize, HandleSize, HandleSize);
}

// ─── paint ───────────────────────────────────────────────────────────────────
void RoiGraphicsItem::paint(QPainter* painter,
                             const QStyleOptionGraphicsItem*,
                             QWidget*)
{
    painter->setRenderHint(QPainter::Antialiasing);

    const QColor tagColor = m_region.tagColor;
    const bool   selected = isSelected();

    // ── Border glow khi selected ──────────────────────────────────────────
    if (selected) {
        QColor glow = tagColor;
        glow.setAlpha(60);
        painter->setPen(QPen(glow, 6));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(m_rect, 4, 4);
    }

    // ── Main border ───────────────────────────────────────────────────────
    QPen pen(tagColor, selected ? 2.0 : 1.5);
    pen.setStyle(Qt::SolidLine);
    painter->setPen(pen);

    QColor fill = tagColor;
    fill.setAlpha(25);
    painter->setBrush(fill);
    painter->drawRoundedRect(m_rect, 4, 4);

    // ── Badge (số thứ tự) ─────────────────────────────────────────────────
    const QString badge = QString::number(m_region.orderNumber);
    const QRectF  badgeRect(m_rect.left(), m_rect.top() - 18, 22, 18);
    painter->setPen(Qt::NoPen);
    painter->setBrush(tagColor);
    painter->drawRoundedRect(badgeRect, 4, 4);
    painter->setPen(Qt::white);
    QFont bf(StyleTheme::FontFamily, 7, QFont::Bold);
    painter->setFont(bf);
    painter->drawText(badgeRect, Qt::AlignCenter, badge);

    // ── Tag label ─────────────────────────────────────────────────────────
    const QString label = m_region.name;
    QFont lf(StyleTheme::FontFamily, 8);
    QFontMetrics fm(lf);
    const int labelW = fm.horizontalAdvance(label) + 12;
    const QRectF labelRect(m_rect.left() + 24, m_rect.top() - 18, labelW, 18);
    QColor labelBg = tagColor;
    labelBg.setAlpha(180);
    painter->setPen(Qt::NoPen);
    painter->setBrush(labelBg);
    painter->drawRoundedRect(labelRect, 4, 4);
    painter->setPen(Qt::white);
    painter->setFont(lf);
    painter->drawText(labelRect, Qt::AlignCenter, label);

    // ── Resize handles ────────────────────────────────────────────────────
    if (selected) {
        painter->setPen(QPen(Qt::white, 1));
        painter->setBrush(tagColor);
        for (const QRectF& hr : handleRects()) {
            painter->drawRoundedRect(hr, 2, 2);
        }
    }
}

// ─── Handles ─────────────────────────────────────────────────────────────────
QList<QRectF> RoiGraphicsItem::handleRects() const
{
    const qreal h = HandleSize;
    const qreal cx = m_rect.center().x();
    const qreal cy = m_rect.center().y();
    const qreal l  = m_rect.left(),   r = m_rect.right();
    const qreal t  = m_rect.top(),    b = m_rect.bottom();

    auto make = [h](qreal x, qreal y) {
        return QRectF(x - h/2, y - h/2, h, h);
    };

    return {
        make(l,  t),   // TopLeft
        make(cx, t),   // TopCenter
        make(r,  t),   // TopRight
        make(l,  cy),  // MiddleLeft
        make(r,  cy),  // MiddleRight
        make(l,  b),   // BottomLeft
        make(cx, b),   // BottomCenter
        make(r,  b),   // BottomRight
    };
}

RoiGraphicsItem::Handle RoiGraphicsItem::hitHandle(const QPointF& pos) const
{
    const auto rects = handleRects();
    for (int i = 0; i < HandleCount; ++i) {
        if (rects[i].contains(pos)) return static_cast<Handle>(i);
    }
    return Handle::None;
}

// ─── Mouse events ─────────────────────────────────────────────────────────────
void RoiGraphicsItem::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressPos  = event->pos();
        m_pressRect = m_rect;
        m_activeHandle = hitHandle(event->pos());

        if (m_activeHandle != Handle::None) {
            // Resize mode: block default move
            event->accept();
            return;
        }
    }
    QGraphicsObject::mousePressEvent(event);
}

void RoiGraphicsItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    if (m_activeHandle != Handle::None) {
        prepareGeometryChange();
        const QPointF delta = event->pos() - m_pressPos;
        m_rect = m_pressRect;
        resizeRect(m_activeHandle, delta);
        // Ensure minimum size
        if (m_rect.width()  < 20) m_rect.setWidth(20);
        if (m_rect.height() < 20) m_rect.setHeight(20);
        update();
        emit geometryChanged();
        event->accept();
        return;
    }
    QGraphicsObject::mouseMoveEvent(event);
    emit geometryChanged();
}

void RoiGraphicsItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    m_activeHandle = Handle::None;
    QGraphicsObject::mouseReleaseEvent(event);
}

void RoiGraphicsItem::hoverMoveEvent(QGraphicsSceneHoverEvent* event)
{
    updateCursor(hitHandle(event->pos()));
}

void RoiGraphicsItem::contextMenuEvent(QGraphicsSceneContextMenuEvent* event)
{
    QMenu menu;
    menu.setStyleSheet(QString(R"(
        QMenu { background: %1; border: 1px solid %2; border-radius: 8px; padding: 4px; }
        QMenu::item { color: %3; padding: 7px 16px; border-radius: 5px; }
        QMenu::item:selected { background: %4; }
    )").arg(StyleTheme::ColorSurfaceHigh)
        .arg(StyleTheme::ColorBorder)
        .arg(StyleTheme::ColorTextPrimary)
        .arg(StyleTheme::ColorSidebarActive));

    QAction* delAct = menu.addAction("🗑  Xóa vùng này");
    if (menu.exec(event->screenPos()) == delAct) {
        emit deleteRequested(this);
    }
}

void RoiGraphicsItem::resizeRect(Handle handle, const QPointF& delta)
{
    switch (handle) {
    case Handle::TopLeft:
        m_rect.setTopLeft(m_pressRect.topLeft() + delta); break;
    case Handle::TopCenter:
        m_rect.setTop(m_pressRect.top() + delta.y()); break;
    case Handle::TopRight:
        m_rect.setTopRight(m_pressRect.topRight() + delta); break;
    case Handle::MiddleLeft:
        m_rect.setLeft(m_pressRect.left() + delta.x()); break;
    case Handle::MiddleRight:
        m_rect.setRight(m_pressRect.right() + delta.x()); break;
    case Handle::BottomLeft:
        m_rect.setBottomLeft(m_pressRect.bottomLeft() + delta); break;
    case Handle::BottomCenter:
        m_rect.setBottom(m_pressRect.bottom() + delta.y()); break;
    case Handle::BottomRight:
        m_rect.setBottomRight(m_pressRect.bottomRight() + delta); break;
    default: break;
    }
}

void RoiGraphicsItem::updateCursor(Handle h)
{
    switch (h) {
    case Handle::TopLeft:
    case Handle::BottomRight: setCursor(Qt::SizeFDiagCursor); break;
    case Handle::TopRight:
    case Handle::BottomLeft:  setCursor(Qt::SizeBDiagCursor); break;
    case Handle::TopCenter:
    case Handle::BottomCenter: setCursor(Qt::SizeVerCursor);  break;
    case Handle::MiddleLeft:
    case Handle::MiddleRight:  setCursor(Qt::SizeHorCursor);  break;
    default: setCursor(Qt::SizeAllCursor); break;
    }
}

QVariant RoiGraphicsItem::itemChange(GraphicsItemChange change, const QVariant& value)
{
    if (change == ItemPositionHasChanged) emit geometryChanged();
    return QGraphicsObject::itemChange(change, value);
}

EZTranslator::TranslationRegion RoiGraphicsItem::toRegion(double canvasW, double canvasH) const
{
    EZTranslator::TranslationRegion r = m_region;
    const QPointF scenePos = pos();
    r.normalizedRect = EZTranslator::NormalizedRect::fromQRectF(
        QRectF(scenePos + m_rect.topLeft(), m_rect.size()),
        canvasW, canvasH);
    return r;
}

void RoiGraphicsItem::setRegion(const EZTranslator::TranslationRegion& region,
                                 double canvasW, double canvasH)
{
    prepareGeometryChange();
    m_region = region;
    const QRectF pxRect = region.normalizedRect.toQRectF(canvasW, canvasH);
    setPos(pxRect.topLeft());
    m_rect = QRectF(QPointF(0, 0), pxRect.size());
    update();
}
