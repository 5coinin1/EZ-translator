#pragma once
#include <QComboBox>

/** Dropdown theo phong cách Dark Card của EZ-Translator */
class AppComboBox : public QComboBox
{
    Q_OBJECT
public:
    explicit AppComboBox(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;
};
