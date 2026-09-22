#pragma once
#include <QWidget>
#include <QList>
#include "core/Types.h"

class QGraphicsView;
class QGraphicsScene;
class QGraphicsPixmapItem;
class QUndoStack;
class RoiGraphicsItem;

/**
 * RegionEditorWindow – Màn hình khoanh vùng dịch.
 *
 * Dùng QGraphicsView Framework.
 * Ở UI phase: nạp mock screenshot từ resources.
 * Sau Phase 8: thay bằng WinWindowCapture.
 */
class RegionEditorWindow : public QWidget
{
    Q_OBJECT
public:
    explicit RegionEditorWindow(QWidget* parent = nullptr);

    /** Nạp danh sách vùng dịch vào scene (dùng khi mở từ profile) */
    void loadRegions(const QList<EZTranslator::TranslationRegion>& regions);

    /** Trả về danh sách vùng dịch đã chỉnh sửa */
    [[nodiscard]] QList<EZTranslator::TranslationRegion> getRegions() const;

signals:
    void saved(const QList<EZTranslator::TranslationRegion>& regions);
    void cancelled();

private slots:
    void onAddRegion();
    void onDeleteRegion();
    void onUndo();
    void onRedo();
    void onSave();

private:
    void buildUi();
    void loadMockScreenshot();
    void addRoiToScene(const EZTranslator::TranslationRegion& region);

    QGraphicsView*        m_view{nullptr};
    QGraphicsScene*       m_scene{nullptr};
    QGraphicsPixmapItem*  m_bgItem{nullptr};
    QUndoStack*           m_undoStack{nullptr};
    QList<RoiGraphicsItem*> m_roiItems;

    int m_nextOrderNumber{1};
};
