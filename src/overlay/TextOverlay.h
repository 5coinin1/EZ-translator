#pragma once

#include <QList>
#include <QRect>
#include <QSize>
#include <QString>
#include <QWidget>

namespace EZTranslator {

/** Một hộp chữ để vẽ: rect theo toạ độ frame + text hiển thị. */
struct OverlayBox
{
    QRect   rect;
    QString text;
};

/**
 * @brief Lớp phủ trong suốt hiển thị chữ (OCR hoặc bản dịch) đè lên cửa sổ đích.
 *
 * - Trong suốt, không viền, luôn trên cùng, **click-through** (chuột/bàn phím
 *   vẫn xuyên qua để dùng ứng dụng gốc).
 * - Toạ độ box theo hệ của frame capture; overlay map tỉ lệ sang vùng client.
 * - Che chữ gốc bằng nền đặc rồi vẽ chữ lên trên.
 */
class TextOverlay : public QWidget
{
    Q_OBJECT
public:
    explicit TextOverlay(QWidget* parent = nullptr);
    ~TextOverlay() override;

    /** Đặt overlay lên vùng client (toạ độ logical) của cửa sổ đích. */
    void showOverTarget(const QRect& clientLogicalRect);

    /** Ẩn overlay (khi dừng dịch / không còn vùng). */
    void hideOverlay();

    /** Cập nhật box cần vẽ (toạ độ theo frame) + kích thước frame tham chiếu. */
    void setBoxes(const QList<OverlayBox>& boxes, const QSize& frameSize);

    /** Xoá nội dung đang vẽ. */
    void clear();

    [[nodiscard]] bool isShowing() const { return isVisible(); }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QRect             m_target;    ///< vùng client (logical) của cửa sổ đích
    QSize             m_frameSize; ///< kích thước frame capture (pixel) để map tỉ lệ
    QList<OverlayBox> m_boxes;     ///< box theo toạ độ frame
};

} // namespace EZTranslator
