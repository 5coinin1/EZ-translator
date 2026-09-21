#include "PrimaryButton.h"
#include "ui/theme/StyleTheme.h"
#include <QEnterEvent>

PrimaryButton::PrimaryButton(const QString& text, QWidget* parent)
    : QPushButton(text, parent)
{
    setMinimumHeight(42);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    applyStyle(false);
}

PrimaryButton::PrimaryButton(const QIcon& icon, const QString& text, QWidget* parent)
    : PrimaryButton(text, parent)
{
    setIcon(icon);
    setIconSize(QSize(18, 18));
}

void PrimaryButton::applyStyle(bool hovered)
{
    const QString bg = hovered ? StyleTheme::ColorPrimaryHover : StyleTheme::ColorPrimary;
    setStyleSheet(QString(R"(
        QPushButton {
            background: qlineargradient(x1:0,y1:0,x2:1,y2:0,
                        stop:0 %1, stop:1 #3b82f6);
            color: white;
            font-size: 11pt;
            font-weight: 600;
            border: none;
            border-radius: 10px;
            padding: 0 20px;
        }
        QPushButton:pressed {
            background: %2;
        }
    )").arg(bg).arg(StyleTheme::ColorPrimaryPressed));
}

void PrimaryButton::enterEvent(QEnterEvent* event)
{
    applyStyle(true);
    QPushButton::enterEvent(event);
}

void PrimaryButton::leaveEvent(QEvent* event)
{
    applyStyle(false);
    QPushButton::leaveEvent(event);
}
