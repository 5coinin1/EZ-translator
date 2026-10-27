#pragma once
#include <QWidget>

class QPushButton;

/**
 * MiniFloatBar – Thanh nổi nhỏ hiển thị khi đang dịch.
 *
 * KHÁC với Translation Overlay!
 * Đây là widget điều khiển EZ-Translator, không phải lớp hiển thị chữ dịch.
 */
class MiniFloatBar : public QWidget
{
    Q_OBJECT
public:
    explicit MiniFloatBar(QWidget* parent = nullptr);

    void setRunning(bool running);

    /** Hiển thị đúng phím tắt dừng dịch (Cài đặt → Phím tắt) trên nút Dừng. */
    void setStopHotkeyText(const QString& keyText);

signals:
    void requestStopTranslation();
    void requestOpenSettings();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    void buildUi();
    void applyState(bool running);

    QWidget* m_statusRow{nullptr};
    QPushButton* m_stopBtn{nullptr}; ///< nút Dừng — chữ chứa phím tắt đang gán
    QString  m_stopHotkey{"F8"};
    bool     m_running{false};
    QPoint   m_dragStart;
};
