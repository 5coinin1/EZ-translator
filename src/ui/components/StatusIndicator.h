#pragma once
#include <QWidget>
#include <QString>
#include <QColor>

/** Chấm tròn màu phát sáng + nhãn trạng thái */
class StatusIndicator : public QWidget
{
    Q_OBJECT
public:
    explicit StatusIndicator(QWidget* parent = nullptr);

    void setState(const QString& label, const QColor& dotColor);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString m_label;
    QColor  m_color{0x22, 0xc5, 0x5e}; // Mặc định: xanh lá (Idle/Ready)
};
