#include "DisplayModeCard.h"
#include "ui/theme/StyleTheme.h"
#include "ui/theme/IconFactory.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>

DisplayModeCard::DisplayModeCard(const QPixmap& iconNormal,
                                 const QPixmap& iconActive,
                                 const QString& title,
                                 const QString& description,
                                 QWidget* parent)
    : QFrame(parent)
    , m_iconNormal(iconNormal)
    , m_iconActive(iconActive)
{
    setObjectName("DisplayModeCard");
    setCursor(Qt::PointingHandCursor);
    setFixedHeight(132);

    auto* mainLay = new QVBoxLayout(this);
    mainLay->setContentsMargins(12, 10, 12, 12);
    mainLay->setSpacing(5);

    // ── Hàng trên cùng: Radio button góc trái ─────────────────────────────────
    auto* topRow = new QHBoxLayout();
    topRow->setContentsMargins(0, 0, 0, 0);
    topRow->setSpacing(0);

    m_radioLabel = new QLabel(this);
    m_radioLabel->setFixedSize(18, 18);
    topRow->addWidget(m_radioLabel);
    topRow->addStretch();
    mainLay->addLayout(topRow);

    // ── Icon trung tâm ───────────────────────────────────────────────────────
    m_iconLabel = new QLabel(this);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    m_iconLabel->setFixedHeight(36);
    mainLay->addWidget(m_iconLabel);

    // ── Tiêu đề ──────────────────────────────────────────────────────────────
    m_titleLabel = new QLabel(title, this);
    m_titleLabel->setAlignment(Qt::AlignCenter);
    mainLay->addWidget(m_titleLabel);

    // ── Mô tả phụ ────────────────────────────────────────────────────────────
    m_descLabel = new QLabel(description, this);
    m_descLabel->setAlignment(Qt::AlignCenter);
    m_descLabel->setWordWrap(true);
    mainLay->addWidget(m_descLabel);

    updateVisuals();
}

void DisplayModeCard::setSelected(bool selected)
{
    if (m_selected == selected) return;
    m_selected = selected;
    updateVisuals();
}

void DisplayModeCard::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        emit clicked();
    }
    QFrame::mousePressEvent(event);
}

void DisplayModeCard::enterEvent(QEnterEvent* event)
{
    m_hovered = true;
    updateVisuals();
    QFrame::enterEvent(event);
}

void DisplayModeCard::leaveEvent(QEvent* event)
{
    m_hovered = false;
    updateVisuals();
    QFrame::leaveEvent(event);
}

void DisplayModeCard::updateVisuals()
{
    // Radio icon
    m_radioLabel->setPixmap(IconFactory::makeRadioCircleIcon(m_selected, 18));

    // Center icon
    m_iconLabel->setPixmap(m_selected ? m_iconActive : m_iconNormal);

    // Styling
    if (m_selected) {
        setStyleSheet(R"(
            QFrame#DisplayModeCard {
                background-color: #132036;
                border: 1.5px solid #3b82f6;
                border-radius: 10px;
            }
        )");
        m_titleLabel->setStyleSheet("color: #ffffff; font-size: 9.5pt; font-weight: 600; border: none; background: transparent;");
        m_descLabel->setStyleSheet("color: #94a3b8; font-size: 8pt; border: none; background: transparent;");
    } else {
        QString borderCol = m_hovered ? "#334155" : "#1e293b";
        QString bgCol = m_hovered ? "#151d2c" : "#111827";
        setStyleSheet(QString(R"(
            QFrame#DisplayModeCard {
                background-color: %1;
                border: 1px solid %2;
                border-radius: 10px;
            }
        )").arg(bgCol, borderCol));
        m_titleLabel->setStyleSheet("color: #cbd5e1; font-size: 9.5pt; font-weight: 600; border: none; background: transparent;");
        m_descLabel->setStyleSheet("color: #64748b; font-size: 8pt; border: none; background: transparent;");
    }
}
