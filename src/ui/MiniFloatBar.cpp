#include "MiniFloatBar.h"
#include "ui/theme/StyleTheme.h"
#include "ui/components/PrimaryButton.h"
#include "ui/components/IconButton.h"
#include "ui/components/StatusIndicator.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QMouseEvent>

MiniFloatBar::MiniFloatBar(QWidget* parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedSize(260, 100);
    buildUi();
}

void MiniFloatBar::buildUi()
{
    // Outer card
    setStyleSheet(QString(R"(
        MiniFloatBar {
            background: transparent;
        }
    )"));

    auto* card = new QWidget(this);
    card->setGeometry(0, 0, 260, 100);
    card->setStyleSheet(QString(R"(
        QWidget {
            background-color: %1;
            border: 1px solid %2;
            border-radius: 12px;
        }
    )").arg(StyleTheme::ColorSurface).arg(StyleTheme::ColorBorder));

    auto* lay = new QVBoxLayout(card);
    lay->setContentsMargins(16, 12, 16, 12);
    lay->setSpacing(10);

    // ── Header row ────────────────────────────────────────────────────────────
    auto* headerRow = new QHBoxLayout();
    auto* appLabel = new QLabel("EZ Translator", card);
    appLabel->setStyleSheet(QString("color: %1; font-size: 10pt; font-weight: 600;")
                                .arg(StyleTheme::ColorTextPrimary));

    auto* settingsBtn = new IconButton("⚙", card);
    settingsBtn->setFixedSize(26, 26);
    connect(settingsBtn, &QPushButton::clicked, this, &MiniFloatBar::requestOpenSettings);

    headerRow->addWidget(appLabel);
    headerRow->addStretch();
    headerRow->addWidget(settingsBtn);
    lay->addLayout(headerRow);

    // ── Status ────────────────────────────────────────────────────────────────
    m_statusRow = new QWidget(card);
    auto* statusLay = new QHBoxLayout(m_statusRow);
    statusLay->setContentsMargins(0, 0, 0, 0);
    statusLay->setSpacing(8);

    auto* dot = new StatusIndicator(m_statusRow);
    dot->setState("Đang dịch...", QColor(StyleTheme::ColorSuccess));
    dot->setFixedWidth(110);

    auto* stopBtn = new PrimaryButton("⏹  Dừng (F8)", card);
    stopBtn->setMinimumHeight(32);
    stopBtn->setStyleSheet(QString(R"(
        QPushButton {
            background: qlineargradient(x1:0,y1:0,x2:1,y2:0,
                        stop:0 #c0392b, stop:1 #e74c3c);
            color: white; font-size: 9pt; font-weight: 600;
            border: none; border-radius: 8px; padding: 0 12px;
        }
        QPushButton:hover { background: #a93226; }
    )"));
    connect(stopBtn, &QPushButton::clicked, this, &MiniFloatBar::requestStopTranslation);

    statusLay->addWidget(dot, 1);
    statusLay->addWidget(stopBtn);
    lay->addWidget(m_statusRow);
}

void MiniFloatBar::setRunning(bool running)
{
    m_running = running;
    setVisible(running);
}

void MiniFloatBar::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        m_dragStart = event->globalPosition().toPoint() - frameGeometry().topLeft();
}

void MiniFloatBar::mouseMoveEvent(QMouseEvent* event)
{
    if (event->buttons() & Qt::LeftButton)
        move(event->globalPosition().toPoint() - m_dragStart);
}
