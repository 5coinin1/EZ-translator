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

    void TextOverlay::setAppearance(const AppSettings &settings)
    {
        m_settings = settings;

        m_settings.fontSize = std::clamp(m_settings.fontSize, 8, 72);

        m_settings.windowOpacity = std::clamp(m_settings.windowOpacity, 10, 100);

        if (!m_settings.windowColor.isValid())
            m_settings.windowColor = QColor(58, 62, 70);

        if (!m_settings.fontColor.isValid())
            m_settings.fontColor = QColor(248, 250, 252);

        // Chữ luôn rõ; độ đục chỉ áp dụng cho nền.
        m_settings.fontColor.setAlpha(255);

        update();
    }

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
        if (m_target.isEmpty() || m_frameSize.isEmpty())
            return;

        QLinearGradient overlayBackground(0.0, 0.0, 0.0, 1.0);
        overlayBackground.setCoordinateMode(QGradient::ObjectBoundingMode);

        QColor base = m_settings.windowColor;

        base.setAlpha(qRound(m_settings.windowOpacity * 255.0 / 100.0));

        overlayBackground.setColorAt(0.00, base.lighter(130));
        overlayBackground.setColorAt(0.45, base);
        overlayBackground.setColorAt(1.00, base.darker(130));

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

        // Chỉ vẽ nền khi khung có nội dung.
        for (const auto &box : m_boxes)
        {
            if (box.text.trimmed().isEmpty())
                continue;

            painter.fillRect(mapped(box), QBrush(overlayBackground));
        }

        painter.setPen(m_settings.fontColor);

        for (const auto &box : m_boxes)
        {
            if (box.text.isEmpty())
                continue;

            const QRectF area = mapped(box).adjusted(1, 0, -1, 0);

            if (area.width() < 2 || area.height() < 2)
                continue;

            const int initialSize =
                std::clamp(int(area.height() * 0.72), 8, m_settings.fontSize);

            for (int pixels = initialSize; pixels >= 7; --pixels)
            {
                QFont font("Segoe UI");
                font.setPixelSize(pixels);
                font.setBold(m_settings.isBold);

                QTextLayout layout(box.text, font);

                QTextOption option;
                option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);

                Qt::Alignment alignment = Qt::AlignLeft;

                if (m_settings.textAlignment == 1)
                    alignment = Qt::AlignHCenter;
                else if (m_settings.textAlignment == 2)
                    alignment = Qt::AlignRight;

                option.setAlignment(alignment);
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

                const bool overflow = height > area.height() || widest > area.width();

                if (overflow && pixels > 7)
                    continue;

                // Nếu vẫn quá dài, thu nhỏ toàn bộ để không mất chữ.
                const qreal scale = std::min({qreal(1), area.height() / height,
                                              area.width() / std::max(qreal(1), widest)});

                painter.save();
                painter.setClipRect(area);

                qreal x = area.left();

                if (m_settings.textAlignment == 1)
                    x += area.width() * (1 - scale) / 2;
                else if (m_settings.textAlignment == 2)
                    x += area.width() * (1 - scale);

                painter.translate(x, area.top() + (area.height() - height * scale) / 2);

                painter.scale(scale, scale);
                layout.draw(&painter, QPointF(0, 0));

                painter.restore();
                break;
            }
        }
    }

} // namespace EZTranslator