#include "overlay/TranslationPopup.h"

#include <QFont>
#include <QGuiApplication>
#include <QLabel>
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
    setFocusPolicy(Qt::NoFocus);

    m_source = new QLabel(this);
    m_source->setWordWrap(true);
    m_source->setTextInteractionFlags(Qt::NoTextInteraction);
    {
        QFont font = m_source->font();
        font.setPixelSize(13);
        m_source->setFont(font);
    }
    m_source->setStyleSheet(QStringLiteral("color:#9aa4b2;"));

    m_body = new QLabel(this);
    m_body->setWordWrap(true);
    m_body->setTextInteractionFlags(Qt::NoTextInteraction);
    {
        QFont font = m_body->font();
        font.setPixelSize(19);
        m_body->setFont(font);
    }
    m_body->setStyleSheet(QStringLiteral("color:#f4f6fa;"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 11, 14, 13);
    layout->setSpacing(7);
    layout->addWidget(m_source);
    layout->addWidget(m_body);

    setStyleSheet(QStringLiteral("TranslationPopup {"
                                 "  background-color: rgba(16,18,24,238);"
                                 "  border: 1px solid rgba(130,150,180,170);"
                                 "  border-radius: 9px;"
                                 "}"));
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
    const bool showSource = !trimmedSource.isEmpty() && trimmedSource != body;
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
