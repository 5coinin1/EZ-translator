#pragma once
#include <QGraphicsObject>
#include <QRectF>
#include <QColor>
#include <QString>
#include "core/Types.h"

/**
 * RoiGraphicsItem – Biểu diễn một vùng dịch (ROI) trên QGraphicsScene.
 *
 * Tính năng:
 *  - 8 resize handles (góc + trung điểm cạnh)
 *  - Move, Resize, Select
 *  - Badge số thứ tự + Tag label
 *  - Hỗ trợ QUndoStack (thông qua itemChange notification)
 */
class RoiGraphicsItem : public QGraphicsObject
{
    Q_OBJECT
public:
    static constexpr int HandleSize = 8;
    static constexpr int HandleCount = 8;

    enum class Handle {
        None = -1,
        TopLeft = 0, TopCenter, TopRight,
        MiddleLeft, MiddleRight,
        BottomLeft, BottomCenter, BottomRight
    };

    explicit RoiGraphicsItem(const EZTranslator::TranslationRegion& region,
                              QGraphicsItem* parent = nullptr);

    // ── QGraphicsItem interface ───────────────────────────────────────────────
    QRectF boundingRect() const override;
    void   paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
                 QWidget* widget = nullptr) override;

    // ── Accessors ─────────────────────────────────────────────────────────────
    EZTranslator::TranslationRegion toRegion(double canvasW, double canvasH) const;
    void setRegion(const EZTranslator::TranslationRegion& region, double canvasW, double canvasH);

    [[nodiscard]] QString regionId()   const { return m_region.id; }
    [[nodiscard]] QRectF  itemRect()   const { return m_rect; }

signals:
    void geometryChanged();
    void deleteRequested(RoiGraphicsItem* item);

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void hoverMoveEvent(QGraphicsSceneHoverEvent* event) override;
    void contextMenuEvent(QGraphicsSceneContextMenuEvent* event) override;

    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;

private:
    QList<QRectF> handleRects() const;
    Handle        hitHandle(const QPointF& pos) const;
    void          resizeRect(Handle handle, const QPointF& delta);
    void          updateCursor(Handle h);

    EZTranslator::TranslationRegion m_region;
    QRectF  m_rect;           // Tọa độ pixel trong scene

    Handle  m_activeHandle{Handle::None};
    QPointF m_pressPos;
    QRectF  m_pressRect;      // Snapshot của rect khi bắt đầu resize/move
};
