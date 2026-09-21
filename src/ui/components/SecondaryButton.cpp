#include "SecondaryButton.h"
#include "ui/theme/StyleTheme.h"

SecondaryButton::SecondaryButton(const QString& text, QWidget* parent)
    : QPushButton(text, parent)
{
    setMinimumHeight(42);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setStyleSheet(QString(R"(
        QPushButton {
            background-color: transparent;
            color: %1;
            font-size: 10pt;
            font-weight: 500;
            border: 1px solid %2;
            border-radius: 10px;
            padding: 0 16px;
        }
        QPushButton:hover {
            background-color: %3;
            border-color: %4;
            color: %1;
        }
        QPushButton:pressed { background-color: %5; }
    )").arg(StyleTheme::ColorTextPrimary)
        .arg(StyleTheme::ColorBorder)
        .arg(StyleTheme::ColorSurface)
        .arg(StyleTheme::ColorBorderFocus)
        .arg(StyleTheme::ColorSurfaceHigh));
}

SecondaryButton::SecondaryButton(const QIcon& icon, const QString& text, QWidget* parent)
    : SecondaryButton(text, parent)
{
    setIcon(icon);
    setIconSize(QSize(20, 20));
}
