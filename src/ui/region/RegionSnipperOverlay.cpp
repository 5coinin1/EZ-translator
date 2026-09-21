#include "RegionSnipperOverlay.h"

#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QFontMetrics>
#include <algorithm>

RegionSnipperOverlay::RegionSnipperOverlay(QWidget* parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setCursor(Qt::CrossCursor);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

void RegionSnipperOverlay::startSnipping(const QPixmap& screenshot, const QRect& targetWindowRect)
{
    m_background = screenshot;
    m_targetRect = targetWindowRect;
    m_isDragging = false;
    m_startPos = QPoint();
    m_currentPos = QPoint();

    // Thiết lập kích thước toàn màn hình
    if (QScreen* screen = QGuiApplication::primaryScreen()) {
        setGeometry(screen->geometry());
    }

    showFullScreen();
    raise();
    activateWindow();
    setFocus();
}

QRect RegionSnipperOverlay::currentSelectionRect() const
{
    int x = std::min(m_startPos.x(), m_currentPos.x());
    int y = std::min(m_startPos.y(), m_currentPos.y());
    int w = std::abs(m_startPos.x() - m_currentPos.x());
    int h = std::abs(m_startPos.y() - m_currentPos.y());
    return {x, y, w, h};
}

void RegionSnipperOverlay::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    // 1. Vẽ ảnh chụp màn hình nền
    if (!m_background.isNull()) {
        p.drawPixmap(rect(), m_background);
    } else {
        p.fillRect(rect(), Qt::black);
    }

    // 2. Làm tối toàn bộ màn hình để tạo hiệu ứng lớp phủ
    p.fillRect(rect(), QColor(10, 15, 29, 120));

    // 3. Làm sáng rõ vùng cửa sổ đích nếu có
    if (m_targetRect.isValid() && m_targetRect.width() > 0 && m_targetRect.height() > 0 && !m_background.isNull()) {
        // Vẽ lại ảnh chụp gốc sáng rõ tại vùng cửa sổ đích
        p.drawPixmap(m_targetRect.topLeft(), m_background, m_targetRect);

        // Viền thanh mảnh nhận diện cửa sổ (không có tiêu đề/badge)
        p.setPen(QPen(QColor(56, 189, 248), 2, Qt::SolidLine));
        p.setBrush(Qt::NoBrush);
        p.drawRect(m_targetRect);
    }

    p.setRenderHint(QPainter::Antialiasing, true);

    // 4. Vẽ vùng đang kéo chuột chọn (Rubberband)
    QRect sel = currentSelectionRect();
    if (sel.isValid() && sel.width() > 2 && sel.height() > 2) {
        // Phủ xanh dương nhẹ trong suốt lên trên nền đã tối sẵn
        p.fillRect(sel, QColor(37, 99, 235, 50));

        // Viền vùng chọn
        p.setPen(QPen(QColor(59, 130, 246), 2, Qt::SolidLine));
        p.setBrush(Qt::NoBrush);
        p.drawRect(sel);

        // Nhãn kích thước (W x H)
        QString sizeText = QString("%1 × %2 px").arg(sel.width()).arg(sel.height());
        QFont sizeFont("Segoe UI", 9, QFont::DemiBold);
        p.setFont(sizeFont);
        QFontMetrics fm(sizeFont);
        int textW = fm.horizontalAdvance(sizeText) + 16;
        int textH = 22;

        int pillX = sel.left();
        int pillY = sel.bottom() + 6;
        if (pillY + textH > height()) {
            pillY = sel.top() - textH - 6;
        }

        QRect pillRect(pillX, pillY, textW, textH);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(37, 99, 235));
        p.drawRoundedRect(pillRect, 4, 4);

        p.setPen(Qt::white);
        p.drawText(pillRect, Qt::AlignCenter, sizeText);
    }

    // 5. Thanh hướng dẫn trên đầu màn hình
    int bannerW = 440;
    int bannerH = 34;
    QRect bannerRect((width() - bannerW) / 2, 20, bannerW, bannerH);

    p.setPen(QPen(QColor(51, 65, 85), 1));
    p.setBrush(QColor(15, 23, 42, 230));
    p.drawRoundedRect(bannerRect, 17, 17);

    QFont bannerFont("Segoe UI", 9, QFont::DemiBold);
    p.setFont(bannerFont);
    p.setPen(QColor(226, 232, 240));
    p.drawText(bannerRect, Qt::AlignCenter, "Giữ chuột trái & kéo để chọn vùng  •  Nhấn ESC để hủy");
}

void RegionSnipperOverlay::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_isDragging = true;
        m_startPos = event->pos();
        m_currentPos = m_startPos;
        update();
    } else if (event->button() == Qt::RightButton) {
        m_isDragging = false;
        hide();
        emit cancelled();
    }
}

void RegionSnipperOverlay::mouseMoveEvent(QMouseEvent* event)
{
    if (m_isDragging) {
        m_currentPos = event->pos();
        update();
    }
}

void RegionSnipperOverlay::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_isDragging) {
        QRect sel = currentSelectionRect();
        m_isDragging = false;
        hide();

        if (sel.width() >= 10 && sel.height() >= 10) {
            if (m_targetRect.isValid() && m_targetRect.width() > 0 && m_targetRect.height() > 0) {
                double nx = (double)(sel.x() - m_targetRect.x()) / m_targetRect.width();
                double ny = (double)(sel.y() - m_targetRect.y()) / m_targetRect.height();
                double nw = (double)sel.width() / m_targetRect.width();
                double nh = (double)sel.height() / m_targetRect.height();

                nx = std::clamp(nx, 0.0, 1.0);
                ny = std::clamp(ny, 0.0, 1.0);
                nw = std::clamp(nw, 0.0, 1.0 - nx);
                nh = std::clamp(nh, 0.0, 1.0 - ny);

                if (nw < 0.01 || nh < 0.01) {
                    double fnx = (double)sel.x() / width();
                    double fny = (double)sel.y() / height();
                    double fnw = (double)sel.width() / width();
                    double fnh = (double)sel.height() / height();
                    EZTranslator::NormalizedRect norm{fnx, fny, fnw, fnh};
                    emit regionSnapped(norm, sel);
                    return;
                }

                EZTranslator::NormalizedRect norm{nx, ny, nw, nh};
                emit regionSnapped(norm, sel);
            } else {
                double nx = (double)sel.x() / width();
                double ny = (double)sel.y() / height();
                double nw = (double)sel.width() / width();
                double nh = (double)sel.height() / height();
                EZTranslator::NormalizedRect norm{nx, ny, nw, nh};
                emit regionSnapped(norm, sel);
            }
        } else {
            emit cancelled();
        }
    }
}

void RegionSnipperOverlay::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        m_isDragging = false;
        hide();
        emit cancelled();
    } else {
        QWidget::keyPressEvent(event);
    }
}
