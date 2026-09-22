#pragma once

#include <QFrame>
#include <QPixmap>
#include <QString>

class QLabel;
class QVBoxLayout;

/**
 * DisplayModeCard – Thẻ lựa chọn chế độ hiển thị bản dịch (Section 3)
 * Có radio button góc trên trái, icon minh họa ở trung tâm, tiêu đề và mô tả.
 */
class DisplayModeCard : public QFrame
{
    Q_OBJECT
public:
    DisplayModeCard(const QPixmap& iconNormal,
                    const QPixmap& iconActive,
                    const QString& title,
                    const QString& description,
                    QWidget* parent = nullptr);

    void setSelected(bool selected);
    [[nodiscard]] bool isSelected() const { return m_selected; }

signals:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void updateVisuals();

    bool m_selected{false};
    bool m_hovered{false};

    QPixmap m_iconNormal;
    QPixmap m_iconActive;

    QLabel* m_radioLabel{nullptr};
    QLabel* m_iconLabel{nullptr};
    QLabel* m_titleLabel{nullptr};
    QLabel* m_descLabel{nullptr};
};
