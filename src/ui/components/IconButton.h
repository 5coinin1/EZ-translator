#pragma once
#include <QPushButton>

/** Nút icon vuông nhỏ (36x36), dùng cho Refresh, Settings, ... */
class IconButton : public QPushButton
{
    Q_OBJECT
public:
    explicit IconButton(const QString& text, QWidget* parent = nullptr);
};
