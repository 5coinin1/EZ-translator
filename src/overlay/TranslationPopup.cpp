#include "overlay/TranslationPopup.h"

#include <QFont>
#include <QGuiApplication>
#include <QLabel>
#include <QPainter>
#include <QRect>
#include <QScreen>
#include <QVBoxLayout>

#include <algorithm>

namespace EZTranslator {

namespace {

constexpr int kMaxBodyWidth = 460; ///< rộng tối đa trước khi text xuống dòng
constexpr int kMinBodyWidth = 200; ///< sàn để popup không bị bóp méo
constexpr int kCursorGapX = 18;
constexpr int kCursorGapY = 22;

} // namespace

TranslationPopup::TranslationPopup(QWidget* parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                          | Qt::WindowDoesNotAcceptFocus | Qt::NoDropShadowWindowHint)
{
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_StyledBackground, true);
    // Cần nền trong suốt để 4 góc bo tròn không bị lộ hình chữ nhật đen.
    setAttribute(Qt::WA_TranslucentBackground, true);
    setFocusPolicy(Qt::NoFocus);

    m_source = new QLabel(this);
    m_source->setWordWrap(true);
    m_source->setTextInteractionFlags(Qt::NoTextInteraction);

    m_body = new QLabel(this);
    m_body->setWordWrap(true);
    m_body->setTextInteractionFlags(Qt::NoTextInteraction);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 11, 14, 13);
    layout->setSpacing(7);
    layout->addWidget(m_source);
    layout->addWidget(m_body);

    applyAppearance();
}

void TranslationPopup::setAppearance(const EZTranslator::AppSettings& settings)
{
    m_appearance = settings;
    applyAppearance();
    if (isVisible()) {
        adjustSize();
    }
}

void TranslationPopup::applyAppearance()
{
    if (!m_source || !m_body)
        return;

    const int bodyPt = qBound(8, m_appearance.fontSize, 48);
    QFont bodyFont(m_appearance.fontFamily, bodyPt);
    bodyFont.setBold(m_appearance.isBold);
    m_body->setFont(bodyFont);

    QFont sourceFont(m_appearance.fontFamily, qMax(8, bodyPt - 4));
    m_source->setFont(sourceFont);

    const int style = qBound(0, m_appearance.textWindowStyle, 3);
    QColor textColor = m_appearance.fontColor.isValid() ? m_appearance.fontColor
                                                        : QColor(244, 246, 250);
    if (style == 3)
        textColor = QColor(26, 26, 26);
    QColor sourceColor = textColor;
    sourceColor.setAlpha(170);
    m_body->setStyleSheet(
        QStringLiteral("color: rgba(%1,%2,%3,%4); background: transparent;")
            .arg(textColor.red())
            .arg(textColor.green())
            .arg(textColor.blue())
            .arg(textColor.alpha()));
    m_source->setStyleSheet(
        QStringLiteral("color: rgba(%1,%2,%3,%4); background: transparent;")
            .arg(sourceColor.red())
            .arg(sourceColor.green())
            .arg(sourceColor.blue())
            .arg(sourceColor.alpha()));

    QColor background = m_appearance.windowColor.isValid()
                            ? m_appearance.windowColor
                            : QColor(16, 18, 24);
    if (style == 2) {
        background.setAlpha(235);
    } else if (style == 3) {
        background = QColor(248, 246, 240);
        background.setAlpha(255);
    } else if (style == 1) {
        const int opacity = qBound(10, m_appearance.windowOpacity, 100);
        background.setAlpha(qBound(70, qRound(255.0 * opacity / 100.0 * 0.72), 220));
    } else {
        background.setAlpha(255);
    }

    const bool outline = style == 2;
    const bool glass = style == 1;
    m_popupBackground = background;
    m_popupBorder = QColor(130, 150, 180, outline ? 220 : 170);
    m_cornerRadius = 9;
    m_drawBorder = outline || glass;
    update();
}

void TranslationPopup::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF area = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setBrush(m_popupBackground);
    if (m_drawBorder)
        painter.setPen(QPen(m_popupBorder, 1.0));
    else
        painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(area, m_cornerRadius, m_cornerRadius);
}

void TranslationPopup::showTranslation(const QString& source, const QString& translation,
                                       const QPoint& cursorScreenPos, int sourceWidthLogical)
{
    const QString body = translation.trimmed().isEmpty() ? source.trimmed() : translation.trimmed();
    if (body.isEmpty()) {
        hidePopup();
        return;
    }

    // Không rộng hơn box nguồn (tránh đè lên nút/khu vực bên cạnh), nhưng đủ hẹp để
    // không bị bóp méo với box rất hẹp (tab ngắn).
    const int maxWidth = sourceWidthLogical > 0
                             ? qBound(kMinBodyWidth, sourceWidthLogical, kMaxBodyWidth)
                             : kMaxBodyWidth;

    m_body->setMaximumWidth(maxWidth);
    m_body->setText(body);

    // Chỉ hiện dòng gốc khi nó thật sự khác bản dịch (tránh lặp hai lần cùng chữ).
    const QString trimmedSource = source.trimmed();
    const bool showSource = m_appearance.textWindowShowSource && !trimmedSource.isEmpty()
                            && trimmedSource != body;
    m_source->setMaximumWidth(maxWidth);
    m_source->setText(trimmedSource);
    m_source->setVisible(showSource);

    adjustSize();
    placeNear(cursorScreenPos);
    show();
    raise();
}

void TranslationPopup::hidePopup()
{
    if (isVisible())
        hide();
}

void TranslationPopup::placeNear(const QPoint& cursorScreenPos)
{
    QScreen* screen = QGuiApplication::screenAt(cursorScreenPos);
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    const QRect area = screen ? screen->availableGeometry() : QRect(0, 0, 1920, 1080);

    // Kẹp kích thước theo màn hình TRƯỚC: nếu popup to hơn vùng màn hình thì phép
    // kẹp toạ độ bên dưới sẽ ép nó về góc trên-trái thay vì cạnh con trỏ.
    QSize popupSize = size();
    popupSize.setWidth(qMin(popupSize.width(), area.width()));
    popupSize.setHeight(qMin(popupSize.height(), area.height()));

    // Mặc định dưới-phải con trỏ; lật sang phía còn lại nếu tràn màn hình.
    int x = cursorScreenPos.x() + kCursorGapX;
    int y = cursorScreenPos.y() + kCursorGapY;

    if (x + popupSize.width() > area.right())
        x = cursorScreenPos.x() - kCursorGapX - popupSize.width();
    if (y + popupSize.height() > area.bottom())
        y = cursorScreenPos.y() - kCursorGapY - popupSize.height();

    x = qBound(area.left(), x, std::max(area.left(), area.right() - popupSize.width()));
    y = qBound(area.top(), y, std::max(area.top(), area.bottom() - popupSize.height()));

    move(x, y);
}

} // namespace EZTranslator
