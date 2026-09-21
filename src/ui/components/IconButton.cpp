#include "IconButton.h"
#include "ui/theme/StyleTheme.h"

IconButton::IconButton(const QString& text, QWidget* parent)
    : QPushButton(text, parent)
{
    setFixedSize(38, 38);
    setCursor(Qt::PointingHandCursor);
    setStyleSheet(QString(R"(
        QPushButton {
            background-color: %1;
            color: %2;
            font-size: 13pt;
            border: 1px solid %3;
            border-radius: 8px;
        }
        QPushButton:hover { background-color: %4; border-color: %5; }
        QPushButton:pressed { background-color: %3; }
    )").arg(StyleTheme::ColorSurface)
        .arg(StyleTheme::ColorTextSecondary)
        .arg(StyleTheme::ColorBorder)
        .arg(StyleTheme::ColorSurfaceHigh)
        .arg(StyleTheme::ColorBorderFocus));
}
