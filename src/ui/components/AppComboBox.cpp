#include "AppComboBox.h"
#include "ui/theme/StyleTheme.h"

#include <QPainter>
#include <QPainterPath>

AppComboBox::AppComboBox(QWidget* parent) : QComboBox(parent)
{
    setMinimumHeight(38);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void AppComboBox::paintEvent(QPaintEvent* event)
{
    QComboBox::paintEvent(event);

    // Vẽ mũi tên chevron xuống thanh mảnh, sắc nét
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(StyleTheme::ColorTextSecondary), 1.8f, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);

    float arrowX = width() - 18.0f;
    float arrowY = height() * 0.5f - 1.5f;
    float arm = 4.0f;

    QPainterPath path;
    path.moveTo(arrowX - arm, arrowY - arm * 0.4f);
    path.lineTo(arrowX, arrowY + arm * 0.6f);
    path.lineTo(arrowX + arm, arrowY - arm * 0.4f);
    p.drawPath(path);
}
