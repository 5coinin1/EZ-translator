#include "StyleTheme.h"

namespace StyleTheme {

QString globalStylesheet()
{
    return QString(R"QSS(
/* ──────────────── Base ──────────────── */
QWidget {
    background-color: %1;
    color: %2;
    font-family: "%3";
    font-size: 10pt;
    border: none;
    outline: none;
}

/* ──────────────── ScrollBar ──────────────── */
QScrollBar:vertical {
    background: %4;
    width: 6px;
    margin: 0;
    border-radius: 3px;
}
QScrollBar::handle:vertical {
    background: %5;
    border-radius: 3px;
    min-height: 30px;
}
QScrollBar::handle:vertical:hover { background: %6; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar:horizontal { height: 0; }

/* ──────────────── QLineEdit ──────────────── */
QLineEdit {
    background-color: %4;
    color: %2;
    border: 1px solid %5;
    border-radius: 8px;
    padding: 6px 10px;
}
QLineEdit:focus { border-color: %7; }
QLineEdit::placeholder { color: %8; }

/* ──────────────── QComboBox ──────────────── */
QComboBox {
    background-color: %4;
    color: %2;
    border: 1px solid %5;
    border-radius: 8px;
    padding: 6px 10px;
    min-height: 32px;
}
QComboBox:hover { border-color: %7; }
QComboBox::drop-down {
    subcontrol-origin: padding;
    subcontrol-position: top right;
    width: 28px;
    border: none;
}
QComboBox::down-arrow {
    image: none;
    width: 0;
    height: 0;
}
QComboBox QAbstractItemView {
    background-color: %10;
    color: %2;
    border: 1px solid %5;
    border-radius: 8px;
    padding: 4px;
    selection-background-color: %11;
    selection-color: %2;
    outline: 0;
}
QComboBox QAbstractItemView::item {
    padding: 6px 10px;
    border-radius: 6px;
    min-height: 28px;
}
QComboBox QAbstractItemView::item:hover {
    background-color: %11;
}

/* ──────────────── QCheckBox ──────────────── */
QCheckBox {
    color: %2;
    spacing: 8px;
    font-size: 10pt;
}
QCheckBox::indicator {
    width: 16px; height: 16px;
    border: 1px solid %5;
    border-radius: 4px;
    background-color: %4;
}
QCheckBox::indicator:checked {
    background-color: %7;
    border-color: %7;
    image: url(:/icons/check.svg);
}
QCheckBox::indicator:hover { border-color: %7; }

/* ──────────────── QSpinBox ──────────────── */
QSpinBox {
    background-color: %4;
    color: %2;
    border: 1px solid %5;
    border-radius: 8px;
    padding: 6px 10px;
}
QSpinBox:focus { border-color: %7; }
QSpinBox::up-button, QSpinBox::down-button { width: 0; }

/* ──────────────── QSlider ──────────────── */
QSlider::groove:horizontal {
    background: %5;
    height: 4px;
    border-radius: 2px;
}
QSlider::handle:horizontal {
    background: %7;
    width: 14px; height: 14px;
    margin: -5px 0;
    border-radius: 7px;
}
QSlider::sub-page:horizontal { background: %7; border-radius: 2px; }

/* ──────────────── QListWidget ──────────────── */
QListWidget {
    background-color: %1;
    border: none;
    outline: 0;
}
QListWidget::item {
    padding: 10px 16px;
    border-radius: 8px;
    color: %9;
}
QListWidget::item:hover { background-color: %12; color: %2; }
QListWidget::item:selected { background-color: %13; color: %2; }

/* ──────────────── QLabel ──────────────── */
QLabel { background: transparent; }

/* ──────────────── QGroupBox ──────────────── */
QGroupBox {
    color: %9;
    font-size: 10pt;
    border: 1px solid %5;
    border-radius: 10px;
    margin-top: 14px;
    padding-top: 8px;
}
QGroupBox::title {
    subcontrol-origin: margin;
    left: 12px;
    padding: 0 4px;
    color: %9;
}

/* ──────────────── QToolTip ──────────────── */
QToolTip {
    background-color: %10;
    color: %2;
    border: 1px solid %5;
    border-radius: 6px;
    padding: 4px 8px;
    font-size: 9pt;
}
)QSS")
        .arg(ColorBackground)       // %1
        .arg(ColorTextPrimary)      // %2
        .arg(FontFamily)            // %3
        .arg(ColorSurface)          // %4
        .arg(ColorBorder)           // %5
        .arg(ColorBorderFocus)      // %6
        .arg(ColorPrimary)          // %7
        .arg(ColorPlaceholder)      // %8
        .arg(ColorTextSecondary)    // %9
        .arg(ColorSurfaceHigh)      // %10
        .arg(ColorSurfaceHigh)      // %11  selection bg (reuse)
        .arg(ColorSidebarHover)     // %12
        .arg(ColorSidebarActive);   // %13
}

QString cardStyle(int radius)
{
    return QString("background-color: %1; border-radius: %2px; border: 1px solid %3;")
        .arg(ColorSurface)
        .arg(radius)
        .arg(ColorBorder);
}

} // namespace StyleTheme
