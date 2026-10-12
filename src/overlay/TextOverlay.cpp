#include "overlay/TextOverlay.h"

#include <QBrush>
#include <QDebug>
#include <QLinearGradient>
#include <QPainter>
#include <QTextLayout>
#include <QTextOption>
#include <algorithm>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace EZTranslator
{

    TextOverlay::TextOverlay(QWidget *parent) : QWidget(parent)
    {
        setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
                       Qt::WindowTransparentForInput | Qt::NoDropShadowWindowHint);

        setAttribute(Qt::WA_TranslucentBackground, true);
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_ShowWithoutActivating, true);
        setFocusPolicy(Qt::NoFocus);

#ifdef _WIN32
        // WDA_EXCLUDEFROMCAPTURE: tránh chụp lại chính overlay.
        if (!SetWindowDisplayAffinity(reinterpret_cast<HWND>(winId()),
                                      DWORD(0x00000011)))
        {
            qWarning() << "[Overlay] Cannot exclude capture:" << GetLastError();
        }
#endif
    }

    TextOverlay::~TextOverlay() = default;

    void TextOverlay::showOverTarget(const QRect &rect)
    {
        if (!rect.isValid() || rect.isEmpty())
        {
            hideOverlay();
            return;
        }

        m_target = rect;

        if (geometry() != rect)
            setGeometry(rect);

        if (!isVisible())
        {
            show();
            raise();
        }
    }

    void TextOverlay::hideOverlay()
    {
        m_boxes.clear();
        m_frameSize = {};
        m_target = {};

        hide();
    }

    void TextOverlay::clear()
    {
        m_boxes.clear();
        update();
    }

    void TextOverlay::setTextBoxes(const QList<OcrTextBox> &boxes,
                                   const QSize &frameSize)
    {
        bool same = frameSize == m_frameSize && boxes.size() == m_boxes.size();

        if (same)
        {
            for (int i = 0; i < boxes.size(); ++i)
            {
                if (boxes[i].rect != m_boxes[i].rect ||
                    boxes[i].text != m_boxes[i].text)
                {
                    same = false;
                    break;
                }
            }
        }

        if (same)
            return;

        m_boxes = boxes;
        m_frameSize = frameSize;
        update();
    }

    void TextOverlay::paintEvent(QPaintEvent *)
    {
        // Nền xám than chuyển sắc nhẹ, vẫn che kín chữ gốc.
        QLinearGradient overlayBackground(0.0, 0.0, 0.0, 1.0);
        overlayBackground.setCoordinateMode(QGradient::ObjectBoundingMode);

        overlayBackground.setColorAt(0.00, QColor(78, 82, 90, 255));
        overlayBackground.setColorAt(0.45, QColor(58, 62, 70, 255));
        overlayBackground.setColorAt(1.00, QColor(44, 48, 56, 255));
        if (m_target.isEmpty() || m_frameSize.isEmpty())
            return;

        QPainter painter(this);
        painter.setRenderHint(QPainter::TextAntialiasing);

        const double sx = double(width()) / m_frameSize.width();

        const double sy = double(height()) / m_frameSize.height();

        const auto mapped = [&](const OcrTextBox &box)
        {
            return QRectF(box.rect.x() * sx, box.rect.y() * sy, box.rect.width() * sx,
                          box.rect.height() * sy)
                .intersected(QRectF(rect()));
        };

        // Chỉ che chữ gốc khi khung đã có nội dung để hiển thị.
        for (const auto &box : m_boxes)
        {
            if (box.text.trimmed().isEmpty())
                continue;

            painter.fillRect(mapped(box), QBrush(overlayBackground));
        }

        painter.setPen(QColor(248, 250, 252));

        for (const auto &box : m_boxes)
        {
            if (box.text.isEmpty())
                continue;

            const QRectF area = mapped(box).adjusted(1, 0, -1, 0);

            if (area.width() < 2 || area.height() < 2)
                continue;

            const int initialSize = std::clamp(int(area.height() * 0.72), 8, 26);

            for (int pixels = initialSize; pixels >= 7; --pixels)
            {
                QFont font("Segoe UI");
                font.setPixelSize(pixels);

                QTextLayout layout(box.text, font);

                QTextOption option;
                option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
                layout.setTextOption(option);

                qreal height = 0;
                qreal widest = 0;

                layout.beginLayout();

                while (true)
                {
                    QTextLine line = layout.createLine();

                    if (!line.isValid())
                        break;

                    line.setLineWidth(area.width());
                    line.setPosition(QPointF(0, height));

                    height += line.height();
                    widest = std::max(widest, line.naturalTextWidth());
                }

                layout.endLayout();

                if (height <= 0)
                    break;

                if ((height > area.height() || widest > area.width()) && pixels > 7)
                {
                    continue;
                }

                const qreal scale = std::min({qreal(1), area.height() / height,
                                              area.width() / std::max(qreal(1), widest)});

                painter.save();
                painter.setClipRect(area);

                painter.translate(area.left(),
                                  area.top() + (area.height() - height * scale) / 2);

                painter.scale(scale, scale);
                layout.draw(&painter, QPointF(0, 0));
                painter.restore();

                break;
            }
        }
    }

} // namespace EZTranslator