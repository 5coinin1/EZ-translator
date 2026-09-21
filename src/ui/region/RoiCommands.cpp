#include "RoiCommands.h"
#include "RoiGraphicsItem.h"
#include <QGraphicsScene>

// ─── DeleteRoiCommand ─────────────────────────────────────────────────────────
DeleteRoiCommand::DeleteRoiCommand(RoiGraphicsItem* item, QGraphicsScene* scene,
                                    QUndoCommand* parent)
    : QUndoCommand("Xóa vùng dịch", parent)
    , m_item(item), m_scene(scene)
{}

void DeleteRoiCommand::undo()
{
    m_scene->addItem(m_item);
}

void DeleteRoiCommand::redo()
{
    m_scene->removeItem(m_item);
}

// ─── MoveRoiCommand ───────────────────────────────────────────────────────────
MoveRoiCommand::MoveRoiCommand(RoiGraphicsItem* item,
                                const QPointF& oldPos, const QPointF& newPos,
                                QUndoCommand* parent)
    : QUndoCommand("Di chuyển vùng dịch", parent)
    , m_item(item), m_oldPos(oldPos), m_newPos(newPos)
{}

void MoveRoiCommand::undo()  { m_item->setPos(m_oldPos); }
void MoveRoiCommand::redo()  { m_item->setPos(m_newPos); }
