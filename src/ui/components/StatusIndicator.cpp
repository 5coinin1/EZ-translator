#include "StatusIndicator.h"
#include "ui/theme/StyleTheme.h"

#include <QPainter>
#include <QHBoxLayout>
#include <QLabel>

StatusIndicator::StatusIndicator(QWidget* parent) : QWidget(parent)
{
    setFixedHeight(22);
    setMinimumWidth(85);
    setState("Sẵn sàng", QColor(StyleTheme::ColorSuccess));
}

void StatusIndicator::setState(const QString& label, const QColor& dotColor)
{
    m_label = label;
    m_color = dotColor;
    updateGeometry();
    update();
}

QSize StatusIndicator::sizeHint() const
{
    QFont f(StyleTheme::FontFamily, StyleTheme::FontSizeBody);
    QFontMetrics fm(f);
    return QSize(22 + fm.horizontalAdvance(m_label), 22);
}

void StatusIndicator::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Dot glow effect
    const int dotR  = 4;
    const int dotX  = dotR + 2;
    const int dotY  = height() / 2;

    QColor glow = m_color;
    glow.setAlpha(45);
    p.setBrush(glow);
    p.setPen(Qt::NoPen);
    p.drawEllipse(QPoint(dotX, dotY), dotR + 3, dotR + 3);

    p.setBrush(m_color);
    p.drawEllipse(QPoint(dotX, dotY), dotR, dotR);

    // Label (hiển thị màu xanh theo trạng thái như trong mockup)
    p.setPen(m_color);
    QFont f(StyleTheme::FontFamily, StyleTheme::FontSizeBody);
    f.setWeight(QFont::DemiBold);
    p.setFont(f);
    p.drawText(dotX + dotR + 6, 0, width() - dotX - dotR - 6, height(), Qt::AlignVCenter | Qt::AlignLeft, m_label);
}
