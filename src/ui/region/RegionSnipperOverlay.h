#pragma once

#include <QWidget>
#include <QRect>
#include <QPoint>
#include <QPixmap>
#include "core/Types.h"

/**
 * @brief RegionSnipperOverlay – Lớp phủ toàn màn hình kiểu Snipping Tool cho phép
 * người dùng dùng chuột kéo chọn vùng dịch trực tiếp trên ảnh chụp màn hình cửa sổ đích.
 * Khắc phục hoàn toàn lỗi lọt click chuột (click-through) và lỗi mất phím ESC.
 */
class RegionSnipperOverlay : public QWidget
{
    Q_OBJECT
public:
    explicit RegionSnipperOverlay(QWidget* parent = nullptr);

    /** Bắt đầu chế độ kéo chọn vùng với ảnh chụp màn hình và vị trí cửa sổ đích */
    void startSnipping(const QPixmap& screenshot, const QRect& targetWindowRect);

signals:
    /** Phát khi người dùng hoàn thành kéo chọn vùng dịch hợp lệ (kèm tọa độ màn hình thực tế) */
    void regionSnapped(const EZTranslator::NormalizedRect& normalizedRect, const QRect& screenRect);

    /** Phát khi người dùng nhấn ESC hoặc click chuột phải để hủy */
    void cancelled();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    QRect currentSelectionRect() const;

    QPixmap m_background;
    QRect   m_targetRect;
    QPoint  m_startPos;
    QPoint  m_currentPos;
    bool    m_isDragging{false};
};
