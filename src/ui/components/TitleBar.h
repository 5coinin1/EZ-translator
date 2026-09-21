#pragma once

#include <QWidget>
#include <QString>

/**
 * TitleBar – Thanh tiêu đề tuỳ biến cho cửa sổ frameless.
 *
 * Chức năng:
 *  - Kéo thả di chuyển cửa sổ cha.
 *  - Nút Minimize / Close.
 *  - Logo icon + tiêu đề + subtitle + slogan (tùy chọn).
 */
class TitleBar : public QWidget
{
    Q_OBJECT
public:
    explicit TitleBar(QWidget* parent = nullptr, bool showMaximize = false);

    /** Bật/tắt nút Maximize */
    void setShowMaximize(bool show);

signals:
    void minimizeRequested();
    void maximizeRequested();
    void closeRequested();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    QPoint m_dragStartPos;
    bool   m_dragging{false};
};
