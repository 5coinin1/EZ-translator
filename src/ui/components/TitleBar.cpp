#include "TitleBar.h"
#include "ui/theme/StyleTheme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QMouseEvent>

TitleBar::TitleBar(QWidget* parent, bool showMaximize)
    : QWidget(parent)
{
    setFixedHeight(52);
    setStyleSheet(QString("background-color: %1;").arg(StyleTheme::ColorBackground));

    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(16, 0, 12, 0);
    lay->setSpacing(10);

    // ── Logo badge ────────────────────────────────────────
    auto* logo = new QLabel("Aあ", this);
    logo->setFixedSize(34, 34);
    logo->setAlignment(Qt::AlignCenter);
    logo->setStyleSheet(QString(R"(
        background: qlineargradient(x1:0,y1:0,x2:1,y2:1,
                    stop:0 #2563eb, stop:1 #3b82f6);
        color: white;
        font-size: 14pt;
        font-weight: 700;
        border-radius: 8px;
    )"));

    // ── Title block ───────────────────────────────────────
    auto* titleBlock = new QWidget(this);
    auto* titleLay   = new QVBoxLayout(titleBlock);
    titleLay->setContentsMargins(0, 0, 0, 0);
    titleLay->setSpacing(1);

    auto* appName = new QLabel("EZ Translator", titleBlock);
    appName->setStyleSheet(QString("color: %1; font-size: 13pt; font-weight: 600;")
                               .arg(StyleTheme::ColorTextPrimary));

    auto* subtitle = new QLabel("Real-time screen translator", titleBlock);
    subtitle->setStyleSheet(QString("color: %1; font-size: 8pt;")
                                .arg(StyleTheme::ColorTextSecondary));

    titleLay->addWidget(appName);
    titleLay->addWidget(subtitle);

    // ── Slogan (right-aligned) ────────────────────────────
    auto* slogan = new QLabel("Play in any language.", this);
    slogan->setStyleSheet(QString("color: %1; font-size: 9pt; font-style: italic;")
                              .arg(StyleTheme::ColorPlaceholder));

    // ── Window control buttons ────────────────────────────
    const QString btnBase = QString(R"(
        QPushButton {
            background: transparent;
            color: %1;
            border: none;
            font-size: 13pt;
            border-radius: 6px;
            min-width: 30px; min-height: 30px;
            max-width: 30px; max-height: 30px;
        }
        QPushButton:hover { background-color: %2; }
    )").arg(StyleTheme::ColorTextSecondary).arg(StyleTheme::ColorSurface);

    auto* btnMin  = new QPushButton("─", this);
    auto* btnMax  = new QPushButton("□", this);
    auto* btnClose = new QPushButton("✕", this);

    btnMin->setStyleSheet(btnBase);
    btnMax->setStyleSheet(btnBase);
    btnClose->setStyleSheet(btnBase + QString(
        "QPushButton:hover { background-color: #c0392b; color: white; }"));

    btnMax->setVisible(showMaximize);

    connect(btnMin,   &QPushButton::clicked, this, &TitleBar::minimizeRequested);
    connect(btnMax,   &QPushButton::clicked, this, &TitleBar::maximizeRequested);
    connect(btnClose, &QPushButton::clicked, this, &TitleBar::closeRequested);

    // ── Assemble ──────────────────────────────────────────
    lay->addWidget(logo);
    lay->addWidget(titleBlock);
    lay->addStretch();
    lay->addWidget(slogan);
    lay->addSpacing(8);
    lay->addWidget(btnMin);
    if (showMaximize) lay->addWidget(btnMax);
    lay->addWidget(btnClose);
}

void TitleBar::setShowMaximize(bool show)
{
    // find btnMax by objectName if needed – left as extension
    Q_UNUSED(show)
}

void TitleBar::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragStartPos = event->globalPosition().toPoint() - window()->frameGeometry().topLeft();
        event->accept();
    }
}

void TitleBar::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        window()->move(event->globalPosition().toPoint() - m_dragStartPos);
        event->accept();
    }
}

void TitleBar::mouseReleaseEvent(QMouseEvent* event)
{
    m_dragging = false;
    event->accept();
}
