#include "RegionEditorWindow.h"
#include "ui/theme/StyleTheme.h"
#include "ui/components/PrimaryButton.h"
#include "ui/components/SecondaryButton.h"
#include "ui/components/IconButton.h"
#include "ui/region/RoiGraphicsItem.h"
#include "ui/region/RoiCommands.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QUndoStack>
#include <QFrame>
#include <QUuid>
#include <QPixmap>

RegionEditorWindow::RegionEditorWindow(QWidget* parent)
    : QWidget(parent)
    , m_undoStack(new QUndoStack(this))
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
    setMinimumSize(860, 600);
    resize(960, 660);
    setStyleSheet(QString("background-color: %1;").arg(StyleTheme::ColorBackground));
    buildUi();
    loadMockScreenshot();

    // Nạp mock ROI mẫu
    QList<EZTranslator::TranslationRegion> mockRegions;

    EZTranslator::TranslationRegion r1;
    r1.id = QUuid::createUuid().toString();
    r1.orderNumber = 1; r1.name = "Menu";
    r1.normalizedRect = {0.03, 0.14, 0.18, 0.66};
    r1.tagColor = QColor(37, 99, 235);
    mockRegions.append(r1);

    EZTranslator::TranslationRegion r2;
    r2.id = QUuid::createUuid().toString();
    r2.orderNumber = 2; r2.name = "Hội thoại";
    r2.normalizedRect = {0.22, 0.68, 0.56, 0.16};
    r2.tagColor = QColor(59, 130, 246);
    mockRegions.append(r2);

    EZTranslator::TranslationRegion r3;
    r3.id = QUuid::createUuid().toString();
    r3.orderNumber = 3; r3.name = "Mô tả vật phẩm";
    r3.normalizedRect = {0.80, 0.62, 0.18, 0.20};
    r3.tagColor = QColor(239, 68, 68);
    mockRegions.append(r3);

    loadRegions(mockRegions);
}

void RegionEditorWindow::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Top bar ───────────────────────────────────────────────────────────────
    auto* topBar = new QWidget(this);
    topBar->setFixedHeight(56);
    topBar->setStyleSheet(QString("background-color: %1; border-bottom: 1px solid %2;")
                              .arg(StyleTheme::ColorBackground).arg(StyleTheme::ColorBorder));

    auto* topLay = new QHBoxLayout(topBar);
    topLay->setContentsMargins(16, 0, 16, 0);
    topLay->setSpacing(12);

    auto* backBtn = new IconButton("←", topBar);
    connect(backBtn, &QPushButton::clicked, this, &RegionEditorWindow::cancelled);

    auto* titleBlock = new QWidget(topBar);
    auto* titleLay   = new QVBoxLayout(titleBlock);
    titleLay->setContentsMargins(0, 0, 0, 0);
    titleLay->setSpacing(2);

    auto* titleLabel = new QLabel("Chỉnh sửa vùng dịch", titleBlock);
    titleLabel->setStyleSheet(QString("color: %1; font-size: 13pt; font-weight: 600;")
                                  .arg(StyleTheme::ColorTextPrimary));
    auto* hintLabel = new QLabel("Kéo thả để tạo vùng, nhấn vào vùng để tùy chỉnh", titleBlock);
    hintLabel->setStyleSheet(QString("color: %1; font-size: 8pt;").arg(StyleTheme::ColorTextSecondary));
    titleLay->addWidget(titleLabel);
    titleLay->addWidget(hintLabel);

    auto* undoBtn = new IconButton("↺", topBar);
    auto* redoBtn = new IconButton("↻", topBar);
    connect(undoBtn, &QPushButton::clicked, this, &RegionEditorWindow::onUndo);
    connect(redoBtn, &QPushButton::clicked, this, &RegionEditorWindow::onRedo);

    auto* saveBtn = new PrimaryButton("💾  Lưu", topBar);
    saveBtn->setFixedWidth(100);
    connect(saveBtn, &QPushButton::clicked, this, &RegionEditorWindow::onSave);

    topLay->addWidget(backBtn);
    topLay->addWidget(titleBlock, 1);
    topLay->addWidget(undoBtn);
    topLay->addWidget(redoBtn);
    topLay->addSpacing(8);
    topLay->addWidget(saveBtn);

    root->addWidget(topBar);

    // ── Canvas ────────────────────────────────────────────────────────────────
    m_scene = new QGraphicsScene(this);
    m_view  = new QGraphicsView(m_scene, this);
    m_view->setRenderHint(QPainter::Antialiasing);
    m_view->setDragMode(QGraphicsView::RubberBandDrag);
    m_view->setStyleSheet(QString("background: %1; border: none;").arg(StyleTheme::ColorBackground));
    m_view->setFrameShape(QFrame::NoFrame);
    root->addWidget(m_view, 1);

    // ── Bottom toolbar ────────────────────────────────────────────────────────
    auto* bottomBar = new QWidget(this);
    bottomBar->setFixedHeight(52);
    bottomBar->setStyleSheet(QString("background-color: %1; border-top: 1px solid %2;")
                                 .arg(StyleTheme::ColorSurface).arg(StyleTheme::ColorBorder));

    auto* bottomLay = new QHBoxLayout(bottomBar);
    bottomLay->setContentsMargins(16, 0, 16, 0);
    bottomLay->setSpacing(10);

    auto makeToolBtn = [&](const QString& text) {
        auto* btn = new QPushButton(text, bottomBar);
        btn->setFixedHeight(36);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(QString(R"(
            QPushButton {
                background-color: %1;
                color: %2;
                border: 1px solid %3;
                border-radius: 8px;
                font-size: 9pt;
                padding: 0 14px;
            }
            QPushButton:hover { background-color: %4; }
        )").arg(StyleTheme::ColorSurface)
            .arg(StyleTheme::ColorTextPrimary)
            .arg(StyleTheme::ColorBorder)
            .arg(StyleTheme::ColorSurfaceHigh));
        return btn;
    };

    auto* addBtn    = makeToolBtn("＋  Thêm vùng");
    auto* autoBtn   = makeToolBtn("✨  Tự tìm vùng chữ (AI)");
    auto* deleteBtn = makeToolBtn("🗑  Xóa vùng");

    connect(addBtn,    &QPushButton::clicked, this, &RegionEditorWindow::onAddRegion);
    connect(deleteBtn, &QPushButton::clicked, this, &RegionEditorWindow::onDeleteRegion);

    auto* countLabel = new QLabel("0 vùng", bottomBar);
    countLabel->setStyleSheet(QString("color: %1; font-size: 9pt;").arg(StyleTheme::ColorTextSecondary));
    connect(m_scene, &QGraphicsScene::changed, this, [this, countLabel]() {
        int n = m_roiItems.count();
        countLabel->setText(QString("%1 vùng").arg(n));
    });

    bottomLay->addWidget(addBtn);
    bottomLay->addWidget(autoBtn);
    bottomLay->addWidget(deleteBtn);
    bottomLay->addStretch();
    bottomLay->addWidget(countLabel);

    root->addWidget(bottomBar);
}

void RegionEditorWindow::loadMockScreenshot()
{
    QPixmap px(":/images/mock_game_screen.jpg");
    if (px.isNull()) {
        // Fallback: tạo ảnh placeholder màu tối
        px = QPixmap(1280, 720);
        px.fill(QColor("#1a1f2e"));
    }
    m_bgItem = m_scene->addPixmap(px);
    m_bgItem->setZValue(-1);
    m_scene->setSceneRect(px.rect());
    m_view->fitInView(m_bgItem, Qt::KeepAspectRatio);
}

void RegionEditorWindow::loadRegions(const QList<EZTranslator::TranslationRegion>& regions)
{
    // Xóa ROI cũ
    for (auto* item : m_roiItems) m_scene->removeItem(item);
    qDeleteAll(m_roiItems);
    m_roiItems.clear();
    m_nextOrderNumber = 1;

    const QRectF sceneR = m_scene->sceneRect();
    for (const auto& region : regions) {
        addRoiToScene(region);
    }
}

void RegionEditorWindow::addRoiToScene(const EZTranslator::TranslationRegion& region)
{
    const QRectF sceneR = m_scene->sceneRect();
    auto* item = new RoiGraphicsItem(region);
    item->setRegion(region, sceneR.width(), sceneR.height());
    connect(item, &RoiGraphicsItem::deleteRequested, this, [this](RoiGraphicsItem* i) {
        m_undoStack->push(new DeleteRoiCommand(i, m_scene));
        m_roiItems.removeAll(i);
    });
    m_scene->addItem(item);
    m_roiItems.append(item);
    m_nextOrderNumber = qMax(m_nextOrderNumber, region.orderNumber + 1);
}

void RegionEditorWindow::onAddRegion()
{
    const QRectF sceneR = m_scene->sceneRect();
    EZTranslator::TranslationRegion newRegion;
    newRegion.id = QUuid::createUuid().toString();
    newRegion.orderNumber = m_nextOrderNumber++;
    newRegion.name = QString("Vùng %1").arg(newRegion.orderNumber);
    newRegion.tagColor = QColor(37, 99, 235);
    newRegion.normalizedRect = {0.3, 0.35, 0.25, 0.12};
    addRoiToScene(newRegion);
}

void RegionEditorWindow::onDeleteRegion()
{
    const auto selected = m_scene->selectedItems();
    for (auto* gi : selected) {
        if (auto* roi = dynamic_cast<RoiGraphicsItem*>(gi)) {
            m_undoStack->push(new DeleteRoiCommand(roi, m_scene));
            m_roiItems.removeAll(roi);
        }
    }
}

void RegionEditorWindow::onUndo() { m_undoStack->undo(); }
void RegionEditorWindow::onRedo() { m_undoStack->redo(); }

void RegionEditorWindow::onSave()
{
    emit saved(getRegions());
    close();
}

QList<EZTranslator::TranslationRegion> RegionEditorWindow::getRegions() const
{
    QList<EZTranslator::TranslationRegion> result;
    const QRectF sceneR = m_scene->sceneRect();
    for (const auto* item : m_roiItems) {
        result.append(item->toRegion(sceneR.width(), sceneR.height()));
    }
    return result;
}
