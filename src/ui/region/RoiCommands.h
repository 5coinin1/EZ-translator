#pragma once
#include <QUndoCommand>
#include <QRectF>
#include <QPointF>

class RoiGraphicsItem;
class QGraphicsScene;

/** Lệnh xóa ROI – Undo sẽ khôi phục item vào scene */
class DeleteRoiCommand : public QUndoCommand
{
public:
    DeleteRoiCommand(RoiGraphicsItem* item, QGraphicsScene* scene,
                     QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;

private:
    RoiGraphicsItem* m_item;
    QGraphicsScene*  m_scene;
};

/** Lệnh di chuyển ROI – Undo sẽ trả về vị trí cũ */
class MoveRoiCommand : public QUndoCommand
{
public:
    MoveRoiCommand(RoiGraphicsItem* item, const QPointF& oldPos, const QPointF& newPos,
                   QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;

private:
    RoiGraphicsItem* m_item;
    QPointF m_oldPos, m_newPos;
};
