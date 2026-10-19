#pragma once

#include <QColor>
#include <QImage>
#include <QList>
#include <QRect>
#include <QSize>
#include <QString>
#include <QWidget>

namespace EZTranslator {

/** Một hộp chữ để vẽ: rect theo toạ độ frame + text + cỡ chữ (pixel frame). */
struct OverlayBox
{
    QRect   rect;
    QList<QRect> coverRects; ///< các dòng glyph nguồn cần che; rỗng → che toàn bộ rect
    QString text;
    int     fontPixelSize{0}; ///< 0 = tự suy từ chiều cao box
    // Smart cover (TextCover): nền đã xoá chữ gốc của cả vùng, giữ nguyên texture/nút/khung UI
    // bên dưới. Rỗng → quay về tô màu đặc (m_coverColor). `plateRect` theo toạ độ frame.
    QImage  plate;
    QRect   plateRect;
    // Màu chữ cho box này. Invalid → dùng màu mặc định của overlay. Smart-plate giữ nền gốc
    // (trang sáng) nên chữ trắng cố định sẽ "trắng xóa"; app đặt màu tương phản theo nền.
    QColor  textColor;
    // Màu che TẠM khi chưa có plate (plate về sau sẽ thay). Invalid → m_coverColor mặc định
    // (navy đậm — trên trang sáng người dùng thấy "cover đen" nhấp nháy). App đặt màu nền vùng.
    QColor  coverColor;
};

/**
 * @brief Lớp phủ trong suốt hiển thị bản dịch/OCR đè lên cửa sổ đích.
 *
 * - Trong suốt, không viền, luôn trên cùng, **click-through**.
 * - Toạ độ box theo frame capture; overlay map tỉ lệ sang vùng client.
 * - **Cover**: tô kín box bằng màu nền để che chữ gốc trước khi vẽ chữ.
 * - Cỡ chữ theo `fontPixelSize` (thường lấy từ chiều cao dòng nguồn), tự co cho vừa box.
 */
class TextOverlay : public QWidget
{
    Q_OBJECT
public:
    explicit TextOverlay(QWidget* parent = nullptr);
    ~TextOverlay() override;

    void showOverTarget(const QRect& clientLogicalRect);
    void hideOverlay();
    void setBoxes(const QList<OverlayBox>& boxes, const QSize& frameSize);
    void clear();

    /** Màu che chữ gốc (nền box). */
    void setCoverColor(const QColor& color);

    /**
     * @brief Kiểu chữ cho bản dịch (màn Cài đặt → Giao diện).
     *
     * @param color               màu chữ mặc định (box có `textColor` riêng thì ưu tiên box).
     * @param bold                in đậm.
     * @param fontPixelSize       cỡ chữ cố định theo pixel frame; 0 = bám theo dòng nguồn.
     * @param alignment           0 = trái, 1 = giữa, 2 = phải.
     * @param lineSpacingFramePx  khoảng cách dòng (pixel frame); 0 = theo chiều cao chữ.
     */
    void setTextStyle(const QColor& color, bool bold, int fontPixelSize, int alignment,
                      int lineSpacingFramePx);

    /** Độ mờ nội dung vẽ (10..100 %) — áp cho cả nền che lẫn chữ dịch. */
    void setContentOpacityPercent(int percent);

    /** Loại cửa sổ overlay khỏi ảnh chụp/quay màn hình (SetWindowDisplayAffinity). */
    void setExcludeFromCapture(bool enable);

    [[nodiscard]] bool isShowing() const { return isVisible(); }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QRect             m_target;
    QSize             m_frameSize;
    QList<OverlayBox> m_boxes;
    QColor            m_coverColor{17, 22, 34, 235};
    QColor            m_textColor{248, 250, 252};
    bool              m_bold{true};
    int               m_fixedPixelSize{0};   ///< 0 = bám theo chiều cao dòng nguồn
    int               m_alignment{0};        ///< 0 trái / 1 giữa / 2 phải
    int               m_lineSpacingPx{0};    ///< pixel frame; 0 = theo chiều cao chữ
    qreal             m_contentOpacity{1.0};
    bool              m_excludeFromCapture{false};
};

} // namespace EZTranslator
