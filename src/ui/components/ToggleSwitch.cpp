#include "ToggleSwitch.h"

#include <QPainter>
#include <QMouseEvent>

ToggleSwitch::ToggleSwitch(QWidget* parent)
    : QAbstractButton(parent)
{
    setCheckable(true);
    setChecked(false);
    setCursor(Qt::PointingHandCursor);
    m_offset = 4;
}

QSize ToggleSwitch::sizeHint() const
{
    return {44, 24};
}

void ToggleSwitch::setOffset(int o)
{
    m_offset = o;
    update();
}

void ToggleSwitch::nextCheckState()
{
    QAbstractButton::nextCheckState();
    m_offset = isChecked() ? (width() - m_thumbRadius * 2 - 4) : 4;
    update();
}

void ToggleSwitch::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        nextCheckState();
        event->accept();
    } else {
        QAbstractButton::mouseReleaseEvent(event);
    }
}

void ToggleSwitch::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    int w = width();
    int h = height();
    int radius = h / 2;

    // Vẽ rãnh trượt (track)
    QColor trackColor = isChecked() ? m_trackOnColor : m_trackOffColor;
    p.setPen(Qt::NoPen);
    p.setBrush(trackColor);
    p.drawRoundedRect(0, 0, w, h, radius, radius);

    // Vẽ núm tròn (thumb)
    int thumbX = isChecked() ? (w - m_thumbRadius * 2 - 3) : 3;
    int thumbY = (h - m_thumbRadius * 2) / 2;

    p.setBrush(m_thumbColor);
    p.drawEllipse(thumbX, thumbY, m_thumbRadius * 2, m_thumbRadius * 2);
}
