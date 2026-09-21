#pragma once
#include <QPixmap>
#include <QColor>
#include <QIcon>

namespace IconFactory
{
    /** Icon tay cầm game solid (Section 1) */
    QPixmap makeGamepadIcon(int size = 22, QColor color = QColor("#cbd5e1"));

    /** Icon dịch thuật 文A vector (Section 2) */
    QPixmap makeTranslateIcon(int size = 22, QColor color = QColor("#cbd5e1"));

    /** Icon hồ sơ tài liệu có góc gấp (Section 3) */
    QPixmap makeProfileIcon(int size = 22, QColor color = QColor("#60a5fa"));

    /** Icon khung quét / cắt vùng dịch [ ] (SecondaryButton) */
    QPixmap makeCropIcon(int size = 20, QColor color = QColor("#38bdf8"));

    /** Icon tam giác Play ▶ (PrimaryButton) */
    QPixmap makePlayIcon(int size = 18, QColor color = Qt::white);

    /** Cờ Việt Nam 🇻🇳 cho combobox ngôn ngữ */
    QPixmap makeVietnamFlag(int width = 22, int height = 15);

    /** Mũi tên chevron xuống ∨ cho combobox */
    QPixmap makeChevronDownIcon(int size = 12, QColor color = QColor("#94a3b8"));

    /** Thumbnail game nhỏ cho combobox cửa sổ (Elden Ring) */
    QPixmap makeGameThumbnailIcon(int size = 22);
}
