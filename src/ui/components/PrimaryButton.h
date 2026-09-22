#pragma once
#include <QPushButton>
#include <QString>

/** Nút bấm chính – gradient xanh dương, dùng cho "Bắt đầu dịch" */
class PrimaryButton : public QPushButton
{
    Q_OBJECT
public:
    explicit PrimaryButton(const QString& text, QWidget* parent = nullptr);
    PrimaryButton(const QIcon& icon, const QString& text, QWidget* parent = nullptr);

protected:
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void applyStyle(bool hovered = false);
};
