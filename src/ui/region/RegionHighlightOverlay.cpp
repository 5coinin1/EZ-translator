#include "RegionHighlightOverlay.h"

#include <QPainter>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QScreen>
#include <QGuiApplication>
#include <QApplication>
#include <QTimer>
#include <QFontMetrics>
#include <cmath>
#include <algorithm>

RegionHighlightOverlay::RegionHighlightOverlay(QWidget* parent)
    : QWidget(parent)
{
    // Exactly the same flags as RegionSnipperOverlay which is confirmed working
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);

    // Pulse animation timer
    m_pulseTimer = new QTimer(this);
    m_pulseTimer->setInterval(50);
    connect(m_pulseTimer, &QTimer::timeout, this, [this]() {
        m_pulseStep = (m_pulseStep + 1) % 60;
        update();
    });

    // Auto-hide timer
    m_autoHideTimer = new QTimer(this);
    m_autoHideTimer->setSingleShot(true);
    connect(m_autoHideTimer, &QTimer::timeout, this, &RegionHighlightOverlay::hideOverlay);
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

void RegionHighlightOverlay::showScreenRect(const QRect& screenRect, int autoHideMs)
{
    // Toggle off if already visible
    if (isVisible()) {
        hideOverlay();
        return;
    }

    if (!screenRect.isValid() || screenRect.width() < 10 || screenRect.height() < 10) {
        return;
    }

    doShow(screenRect, autoHideMs);
}

void RegionHighlightOverlay::showRegion(const QRect& targetWindowRect,
                                         const EZTranslator::NormalizedRect& normalizedRegion,
                                         int autoHideMs)
{
    // Toggle off if already visible
    if (isVisible()) {
        hideOverlay();
        return;
    }

    if (!targetWindowRect.isValid() || targetWindowRect.width() < 10 || targetWindowRect.height() < 10) {
        return;
    }

    int rx = targetWindowRect.x() + qRound(normalizedRegion.x * targetWindowRect.width());
    int ry = targetWindowRect.y() + qRound(normalizedRegion.y * targetWindowRect.height());
    int rw = qRound(normalizedRegion.width  * targetWindowRect.width());
    int rh = qRound(normalizedRegion.height * targetWindowRect.height());

    doShow(QRect(rx, ry, rw, rh), autoHideMs);
}

void RegionHighlightOverlay::hideOverlay()
{
    m_pulseTimer->stop();
    m_autoHideTimer->stop();
    hide();
    emit closed();
}

// ─────────────────────────────────────────────────────────────────────────────
// Private implementation
// ─────────────────────────────────────────────────────────────────────────────

void RegionHighlightOverlay::doShow(const QRect& screenRect, int autoHideMs)
{
    // 1. Grab current screen screenshot as background (same as RegionSnipperOverlay)
    if (QScreen* screen = QGuiApplication::primaryScreen()) {
        m_background = screen->grabWindow(0);
        setGeometry(screen->geometry());
    }

    // 2. Store the highlight rect in screen coords
    m_highlightRect = screenRect;

    m_pulseStep = 0;
    m_pulseTimer->start();

    // 3. Show fullscreen exactly like RegionSnipperOverlay
    showFullScreen();
    raise();
    activateWindow();
    setFocus();

    if (autoHideMs > 0) {
        m_autoHideTimer->start(autoHideMs);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Painting
// ─────────────────────────────────────────────────────────────────────────────

void RegionHighlightOverlay::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    // 1. Draw screenshot background
    if (!m_background.isNull()) {
        p.drawPixmap(rect(), m_background);
    } else {
        p.fillRect(rect(), QColor(15, 23, 42));
    }

    if (!m_highlightRect.isValid()) return;

    // 2. Dim everything OUTSIDE the region (dark overlay)
    QRegion outside(rect());
    outside -= QRegion(m_highlightRect);
    p.setClipRegion(outside);
    p.fillRect(rect(), QColor(0, 0, 0, 130));
    p.setClipping(false);

    // 3. Draw red border on region
    p.setRenderHint(QPainter::Antialiasing);

    // Animated pulse
    float pulse = 0.5f * (1.0f + std::cos(m_pulseStep * 0.105f));
    int alpha = 210 + qRound(pulse * 45.0f);
    QColor redColor(239, 68, 68, alpha);

    p.setPen(QPen(redColor, 3.0f, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
    p.setBrush(QColor(239, 68, 68, 18));
    p.drawRect(m_highlightRect);

    // 4. Eight white resize handles
    const int hs  = 9;
    const int hhs = hs / 2;
    QRect hr = m_highlightRect;
    const QList<QPoint> handles = {
        hr.topLeft(),
        QPoint(hr.center().x(), hr.top()),
        hr.topRight(),
        QPoint(hr.left(),  hr.center().y()),
        QPoint(hr.right(), hr.center().y()),
        hr.bottomLeft(),
        QPoint(hr.center().x(), hr.bottom()),
        hr.bottomRight()
    };
    p.setPen(QPen(redColor, 1.5f));
    p.setBrush(Qt::white);
    for (const QPoint& pt : handles) {
        p.drawRect(QRect(pt.x() - hhs, pt.y() - hhs, hs, hs));
    }

    // 5. Badge: red circle "1" + dark label
    QFont badgeFont("Segoe UI", 9, QFont::Bold);
    p.setFont(badgeFont);
    int badgeY = hr.top() - 28;
    if (badgeY < 6) badgeY = hr.top() + 8;
    int badgeX = hr.left();
    int dia = 22;

    QRect circleR(badgeX, badgeY, dia, dia);
    p.setPen(Qt::NoPen);
    p.setBrush(redColor);
    p.drawEllipse(circleR);
    p.setPen(Qt::white);
    p.drawText(circleR, Qt::AlignCenter, "1");

    QString lbl = "Vùng dịch";
    QFontMetrics fm(badgeFont);
    int tw = fm.horizontalAdvance(lbl);
    QRect pillR(badgeX + dia + 4, badgeY, tw + 18, dia);
    p.setPen(QPen(redColor, 1.0f));
    p.setBrush(QColor(15, 23, 42, 230));
    p.drawRoundedRect(pillR, 4, 4);
    p.setPen(Qt::white);
    p.drawText(pillR, Qt::AlignCenter, lbl);

    // 6. Bottom instruction bar
    QFont guideFont("Segoe UI", 9);
    p.setFont(guideFont);
    QString guide = "  Nhấn ESC hoặc click để đóng  ";
    QFontMetrics gfm(guideFont);
    int gw = gfm.horizontalAdvance(guide) + 24;
    int gh = gfm.height() + 10;
    int gx = (width() - gw) / 2;
    int gy = height() - gh - 24;
    QRect gPill(gx, gy, gw, gh);
    p.setBrush(QColor(15, 23, 42, 210));
    p.setPen(QPen(QColor(51, 65, 85), 1.0f));
    p.drawRoundedRect(gPill, 6, 6);
    p.setPen(QColor("#cbd5e1"));
    p.drawText(gPill, Qt::AlignCenter, guide);
}

void RegionHighlightOverlay::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        hideOverlay();
    }
    QWidget::keyPressEvent(event);
}

void RegionHighlightOverlay::mousePressEvent(QMouseEvent*)
{
    hideOverlay();
}
