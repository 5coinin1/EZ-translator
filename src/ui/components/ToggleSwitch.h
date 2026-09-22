#pragma once

#include <QAbstractButton>
#include <QColor>

/**
 * @brief ToggleSwitch – Công tắc gạt ON/OFF phong cách hiện đại (Translumo / iOS).
 */
class ToggleSwitch : public QAbstractButton
{
    Q_OBJECT
    Q_PROPERTY(int offset READ offset WRITE setOffset)

public:
    explicit ToggleSwitch(QWidget* parent = nullptr);

    [[nodiscard]] QSize sizeHint() const override;

    int offset() const { return m_offset; }
    void setOffset(int o);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void nextCheckState() override;

private:
    int m_offset{4};
    int m_thumbRadius{8};
    QColor m_trackOnColor{QColor("#38bdf8")};
    QColor m_trackOffColor{QColor("#334155")};
    QColor m_thumbColor{Qt::white};
};
