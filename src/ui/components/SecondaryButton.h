#pragma once
#include <QPushButton>

/** Nút bấm thứ cấp – viền outline, dùng cho "Chỉnh sửa vùng dịch" */
class SecondaryButton : public QPushButton
{
    Q_OBJECT
public:
    explicit SecondaryButton(const QString& text, QWidget* parent = nullptr);
    SecondaryButton(const QIcon& icon, const QString& text, QWidget* parent = nullptr);
};
