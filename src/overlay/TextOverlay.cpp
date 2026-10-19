#include "overlay/TextOverlay.h"

#include "platform/PlatformUtils.h"

#include <QFont>
#include <QPainter>
#include <QTextLayout>
#include <QTextOption>

#include <algorithm>
#include <cmath>

namespace EZTranslator {

namespace {

/**
 * @brief Break chữ thành dòng và đặt vị trí từng dòng với khoảng cách dòng cố định.
 *
 * Trả tổng chiều cao đã dùng (để vòng lặp co cỡ chữ biết có tràn box hay không).
 * @param lineAdvance chiều cao mỗi dòng (pixel widget); <= 0 = theo chiều cao chữ.
 */
qreal layoutParagraph(QTextLayout& layout, const QString& text, int wrapWidth, int lineAdvance)
{
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    layout.setTextOption(option);

    layout.beginLayout();
    qreal y = 0.0;
    while (true) {
        QTextLine line = layout.createLine();
        if (!line.isValid())
            break;
        line.setLineWidth(qreal(std::max(8, wrapWidth)));
        line.setPosition(QPointF(0.0, y));
        y += lineAdvance > 0 ? qreal(lineAdvance) : line.height();
    }
    layout.endLayout();
    return y;
}

} // namespace

TextOverlay::TextOverlay(QWidget* parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                   | Qt::WindowTransparentForInput | Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setFocusPolicy(Qt::NoFocus);
}

TextOverlay::~TextOverlay() = default;

void TextOverlay::showOverTarget(const QRect& clientLogicalRect)
{
    if (!clientLogicalRect.isValid() || clientLogicalRect.isEmpty()) {
        hideOverlay();
        return;
    }

    m_target = clientLogicalRect;
    if (geometry() != clientLogicalRect)
        setGeometry(clientLogicalRect);

    if (!isVisible())
        show();
    raise();
}

void TextOverlay::hideOverlay()
{
    m_boxes.clear();
    m_frameSize = QSize();
    m_target = QRect();
    hide();
}

void TextOverlay::setBoxes(const QList<OverlayBox>& boxes, const QSize& frameSize)
{
    m_boxes = boxes;
    m_frameSize = frameSize;
    update();
}

void TextOverlay::clear()
{
    m_boxes.clear();
    update();
}

void TextOverlay::setCoverColor(const QColor& color)
{
    if (color.isValid())
        m_coverColor = color;
}

void TextOverlay::setTextStyle(const QColor& color, bool bold, int fontPixelSize, int alignment,
                               int lineSpacingFramePx)
{
    if (color.isValid())
        m_textColor = color;
    m_bold = bold;
    m_fixedPixelSize = std::max(0, fontPixelSize);
    m_alignment = std::clamp(alignment, 0, 2);
    m_lineSpacingPx = std::max(0, lineSpacingFramePx);
    update();
}

void TextOverlay::setContentOpacityPercent(int percent)
{
    m_contentOpacity = qBound(0.05, double(percent) / 100.0, 1.0);
    update();
}

void TextOverlay::setExcludeFromCapture(bool enable)
{
    m_excludeFromCapture = enable;
    // winId() buộc tạo HWND native ngay cả khi overlay chưa từng hiện.
    EZTranslator::Platform::setExcludedFromCapture(quintptr(winId()), enable);
}

void TextOverlay::paintEvent(QPaintEvent*)
{
    if (m_target.isEmpty() || m_frameSize.isEmpty())
        return;

    const double scaleX = double(width()) / double(m_frameSize.width());
    const double scaleY = double(height()) / double(m_frameSize.height());

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setOpacity(m_contentOpacity);

    // HAI LƯỢT: che (plate/màu đặc) của MỌI box trước, rồi mới vẽ chữ của mọi box.
    // Vẽ che-rồi-chữ trong CÙNG một vòng lặp làm box sau dán plate đè lên chữ của box trước
    // ("chữ nằm dưới phần layout, không nhìn thấy") — vì mọi box của một vùng dùng chung một plate.
    for (const OverlayBox& box : m_boxes) {
        if (box.text.isEmpty())
            continue;

        const QRectF target(box.rect.x() * scaleX, box.rect.y() * scaleY,
                            box.rect.width() * scaleX, box.rect.height() * scaleY);
        if (target.width() < 2.0 || target.height() < 2.0)
            continue;

        // Che chữ gốc. Ưu tiên PLATE (nền đã xoá chữ, giữ UI bên dưới); nếu chưa có plate thì
        // tô màu đặc như trước. Paragraph draw rect có thể rộng/cao hơn đến paragraph bounds
        // để bản dịch wrap tự nhiên như reference.
        const QColor cover = box.coverColor.isValid() ? box.coverColor : m_coverColor;
        if (!box.plate.isNull() && box.plateRect.isValid()) {
            const QRectF plateTarget(box.plateRect.x() * scaleX, box.plateRect.y() * scaleY,
                                     box.plateRect.width() * scaleX,
                                     box.plateRect.height() * scaleY);
            painter.drawImage(plateTarget, box.plate);
        } else if (box.coverRects.isEmpty()) {
            painter.fillRect(target, cover);
        } else {
            for (const QRect& coverRect : box.coverRects) {
                const QRectF coverTarget(coverRect.x() * scaleX, coverRect.y() * scaleY,
                                         coverRect.width() * scaleX,
                                         coverRect.height() * scaleY);
                painter.fillRect(coverTarget, cover);
            }
        }
    }

    for (const OverlayBox& box : m_boxes) {
        if (box.text.isEmpty())
            continue;

        const QRectF target(box.rect.x() * scaleX, box.rect.y() * scaleY,
                            box.rect.width() * scaleX, box.rect.height() * scaleY);
        if (target.width() < 2.0 || target.height() < 2.0)
            continue;

        // Cỡ chữ: ưu tiên cỡ cố định từ Cài đặt (nếu người dùng tắt "bám chữ gốc"),
        // không thì theo chiều cao dòng nguồn — sau đó co dần cho vừa box.
        int pixelSize = 0;
        if (m_fixedPixelSize > 0)
            pixelSize = int(std::lround(m_fixedPixelSize * scaleY));
        else if (box.fontPixelSize > 0)
            pixelSize = int(std::lround(box.fontPixelSize * scaleY));
        else
            pixelSize = int(target.height() * 0.5);
        pixelSize = std::max(6, pixelSize);

        QFont font = painter.font();
        font.setBold(m_bold);

        const int lineAdvance =
            m_lineSpacingPx > 0 ? std::max(6, int(std::lround(m_lineSpacingPx * scaleY))) : 0;
        const QRect textRect = target.adjusted(2, 1, -2, -1).toRect();

        QTextLayout layout;
        for (int attempt = 0;; ++attempt) {
            font.setPixelSize(pixelSize);
            layout.setText(box.text);
            layout.setFont(font);
            const qreal neededHeight =
                layoutParagraph(layout, box.text, textRect.width(), lineAdvance);
            if (neededHeight <= std::max(8, textRect.height()) || pixelSize <= 6 || attempt >= 24)
                break;
            pixelSize = std::max(6, int(pixelSize * 0.93));
        }

        painter.setFont(font);
        painter.setPen(box.textColor.isValid() ? box.textColor : m_textColor);

        // Vẽ từng dòng thủ công để tôn trọng khoảng cách dòng (QPainter::drawText không nhận
        // line-height của QTextOption) và căn lề theo Cài đặt.
        for (int i = 0; i < layout.lineCount(); ++i) {
            const QTextLine line = layout.lineAt(i);
            qreal x = textRect.left();
            if (m_alignment == 1)
                x += (textRect.width() - line.naturalTextWidth()) / 2.0;
            else if (m_alignment == 2)
                x += textRect.width() - line.naturalTextWidth();
            const QString lineText =
                box.text.mid(line.textStart(), line.textLength());
            painter.drawText(QPointF(x, textRect.top() + line.y() + line.ascent()), lineText);
        }
    }
}

} // namespace EZTranslator
