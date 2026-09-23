#include "overlay/TextOverlay.h"

#include <QFont>
#include <QPainter>

#include <algorithm>

namespace EZTranslator {

namespace {

const QColor kBackground(17, 22, 34, 235); // che chu goc
const QColor kTextColor(248, 250, 252);

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

void TextOverlay::setTextBoxes(const QList<OcrTextBox>& boxes, const QSize& frameSize)
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

void TextOverlay::paintEvent(QPaintEvent*)
{
    if (m_target.isEmpty() || m_frameSize.isEmpty())
        return;

    const double scaleX = double(width()) / double(m_frameSize.width());
    const double scaleY = double(height()) / double(m_frameSize.height());

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    for (const OcrTextBox& box : m_boxes) {
        if (box.text.isEmpty())
            continue;

        const QRectF target(box.rect.x() * scaleX, box.rect.y() * scaleY,
                            box.rect.width() * scaleX, box.rect.height() * scaleY);
        if (target.width() < 2.0 || target.height() < 2.0)
            continue;

        // Che chữ gốc.
        painter.fillRect(target, kBackground);

        QFont font = painter.font();
        font.setBold(true);
        font.setPixelSize(std::max(8, int(target.height() * 0.68)));
        painter.setFont(font);
        painter.setPen(kTextColor);
        painter.drawText(target.adjusted(2, 1, -2, -1),
                         Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap, box.text);
    }
}

} // namespace EZTranslator
