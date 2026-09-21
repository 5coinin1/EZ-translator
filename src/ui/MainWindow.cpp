#include "MainWindow.h"
#include "ui/theme/StyleTheme.h"
#include "ui/components/TitleBar.h"
#include "ui/components/SectionTitle.h"
#include "ui/components/AppComboBox.h"
#include "ui/components/PrimaryButton.h"
#include "ui/components/SecondaryButton.h"
#include "ui/components/IconButton.h"
#include "ui/components/StatusIndicator.h"
#include "ui/theme/IconFactory.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFrame>

MainWindow::MainWindow(QWidget* parent)
    : QWidget(parent)
{
    setWindowTitle("EZ-Translator");
    setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
    setAttribute(Qt::WA_TranslucentBackground, false);
    setMinimumSize(440, 480);
    resize(480, 530);
    setStyleSheet(QString("QWidget#MainWindow { background-color: %1; border-radius: 12px; }")
                      .arg(StyleTheme::ColorBackground));
    setObjectName("MainWindow");
    buildUi();
    populateMockData();
}

void MainWindow::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── TitleBar ─────────────────────────────────────────────────────────────
    m_titleBar = new TitleBar(this);
    connect(m_titleBar, &TitleBar::minimizeRequested, this, &QWidget::showMinimized);
    connect(m_titleBar, &TitleBar::closeRequested,    this, &QWidget::close);
    root->addWidget(m_titleBar);

    // ── Divider ───────────────────────────────────────────────────────────────
    auto* divider = new QFrame(this);
    divider->setFixedHeight(1);
    divider->setStyleSheet(QString("background-color: %1;").arg(StyleTheme::ColorBorder));
    root->addWidget(divider);

    // ── Content ───────────────────────────────────────────────────────────────
    auto* content = new QWidget(this);
    auto* contentLay = new QVBoxLayout(content);
    contentLay->setContentsMargins(StyleTheme::SpacingXL, StyleTheme::SpacingL,
                                   StyleTheme::SpacingXL, StyleTheme::SpacingL);
    contentLay->setSpacing(StyleTheme::SpacingL);

    // ── Section 1: Chọn cửa sổ ───────────────────────────────────────────────
    contentLay->addWidget(new SectionTitle(SectionIconType::Gamepad, "1. Chọn cửa sổ cần dịch", content));

    auto* windowRow = new QHBoxLayout();
    windowRow->setSpacing(StyleTheme::SpacingS);
    m_windowCombo = new AppComboBox(content);
    auto* refreshBtn = new IconButton("⟳", content);
    windowRow->addWidget(m_windowCombo, 1);
    windowRow->addWidget(refreshBtn);
    contentLay->addLayout(windowRow);

    // ── Section 2: Ngôn ngữ ──────────────────────────────────────────────────
    contentLay->addWidget(new SectionTitle(SectionIconType::Translate, "2. Ngôn ngữ", content));

    auto* langRow = new QHBoxLayout();
    langRow->setSpacing(StyleTheme::SpacingS);

    auto* langBlock = new QWidget(content);
    auto* langBlockLay = new QVBoxLayout(langBlock);
    langBlockLay->setContentsMargins(0, 0, 0, 0);
    langBlockLay->setSpacing(4);

    auto* srcLabel = new QLabel("Ngôn ngữ gốc", langBlock);
    srcLabel->setStyleSheet(QString("color: %1; font-size: 8pt;").arg(StyleTheme::ColorTextSecondary));
    m_srcLangCombo = new AppComboBox(langBlock);
    langBlockLay->addWidget(srcLabel);
    langBlockLay->addWidget(m_srcLangCombo);

    auto* swapBtn = new IconButton("⇄", content);
    swapBtn->setToolTip("Hoán đổi ngôn ngữ");

    auto* dstBlock = new QWidget(content);
    auto* dstBlockLay = new QVBoxLayout(dstBlock);
    dstBlockLay->setContentsMargins(0, 0, 0, 0);
    dstBlockLay->setSpacing(4);

    auto* dstLabel = new QLabel("Ngôn ngữ đích", dstBlock);
    dstLabel->setStyleSheet(QString("color: %1; font-size: 8pt;").arg(StyleTheme::ColorTextSecondary));
    m_dstLangCombo = new AppComboBox(dstBlock);
    dstBlockLay->addWidget(dstLabel);
    dstBlockLay->addWidget(m_dstLangCombo);

    langRow->addWidget(langBlock, 1);
    langRow->addWidget(swapBtn);
    langRow->addWidget(dstBlock, 1);
    contentLay->addLayout(langRow);

    // ── Section 3: Hồ sơ ─────────────────────────────────────────────────────
    contentLay->addWidget(new SectionTitle(SectionIconType::Profile, "3. Hồ sơ (Profile)", content));

    auto* profileRow = new QHBoxLayout();
    profileRow->setSpacing(StyleTheme::SpacingS);
    m_profileCombo = new AppComboBox(content);

    auto* newProfileBtn = new QPushButton("＋ Tạo mới", content);
    newProfileBtn->setFixedHeight(38);
    newProfileBtn->setStyleSheet(QString(R"(
        QPushButton {
            background-color: %1;
            color: %2;
            border: 1px solid %3;
            border-radius: 8px;
            font-size: 9pt;
            padding: 0 12px;
        }
        QPushButton:hover { background-color: %4; }
    )").arg(StyleTheme::ColorSurface)
        .arg(StyleTheme::ColorTextPrimary)
        .arg(StyleTheme::ColorBorder)
        .arg(StyleTheme::ColorSurfaceHigh));

    auto* manageProfileBtn = new QPushButton("📁 Quản lý", content);
    manageProfileBtn->setFixedHeight(38);
    manageProfileBtn->setStyleSheet(newProfileBtn->styleSheet());

    profileRow->addWidget(m_profileCombo, 1);
    profileRow->addWidget(newProfileBtn);
    profileRow->addWidget(manageProfileBtn);
    contentLay->addLayout(profileRow);

    contentLay->addStretch();

    // ── Action buttons ────────────────────────────────────────────────────────
    auto* actionRow = new QHBoxLayout();
    actionRow->setSpacing(StyleTheme::SpacingS);

    auto* editBtn  = new SecondaryButton(QIcon(IconFactory::makeCropIcon(20, QColor("#38bdf8"))), "  Chỉnh sửa vùng dịch", content);
    auto* startBtn = new PrimaryButton(QIcon(IconFactory::makePlayIcon(16, Qt::white)), "  Bắt đầu dịch (F8)", content);

    actionRow->addWidget(editBtn,  1);
    actionRow->addWidget(startBtn, 2);
    contentLay->addLayout(actionRow);

    connect(editBtn,  &QPushButton::clicked, this, &MainWindow::requestOpenRegionEditor);
    connect(startBtn, &QPushButton::clicked, this, &MainWindow::requestStartTranslation);

    root->addWidget(content, 1);

    // ── Footer ────────────────────────────────────────────────────────────────
    auto* footer = new QWidget(this);
    footer->setFixedHeight(40);
    footer->setStyleSheet(QString("background-color: %1; border-bottom-left-radius: 12px; border-bottom-right-radius: 12px;")
                              .arg(StyleTheme::ColorSurface));

    auto* footerLay = new QHBoxLayout(footer);
    footerLay->setContentsMargins(StyleTheme::SpacingXL, 0, StyleTheme::SpacingM, 0);
    footerLay->setSpacing(StyleTheme::SpacingM);

    m_statusIndicator = new StatusIndicator(footer);

    auto* sep1 = new QLabel("•", footer);
    sep1->setStyleSheet(QString("color: %1; font-size: 7pt;").arg(StyleTheme::ColorBorder));
    auto* regionCount = new QLabel("5 vùng", footer);
    regionCount->setStyleSheet(QString("color: %1; font-size: 9pt;").arg(StyleTheme::ColorTextSecondary));

    auto* sep2 = new QLabel("•", footer);
    sep2->setStyleSheet(sep1->styleSheet());
    auto* cacheLabel = new QLabel("Bộ nhớ đệm: 124 câu", footer);
    cacheLabel->setStyleSheet(regionCount->styleSheet());

    footerLay->addWidget(m_statusIndicator);
    footerLay->addWidget(sep1);
    footerLay->addWidget(regionCount);
    footerLay->addWidget(sep2);
    footerLay->addWidget(cacheLabel);
    footerLay->addStretch();

    auto* settingsBtn = new QPushButton("⚙  Cài đặt", footer);
    settingsBtn->setFixedHeight(26);
    settingsBtn->setStyleSheet(QString(R"(
        QPushButton {
            background: transparent;
            color: %1;
            font-size: 9pt;
            border: none;
        }
        QPushButton:hover { color: %2; }
    )").arg(StyleTheme::ColorTextSecondary).arg(StyleTheme::ColorTextPrimary));
    connect(settingsBtn, &QPushButton::clicked, this, &MainWindow::requestOpenSettings);
    footerLay->addWidget(settingsBtn);

    root->addWidget(footer);
}

void MainWindow::populateMockData()
{
    // Mock: danh sách cửa sổ
    m_windowCombo->addItem(QIcon(IconFactory::makeGameThumbnailIcon(22)), "ELDEN RING™");
    m_windowCombo->addItem("Notepad");
    m_windowCombo->addItem("Google Chrome");

    // Mock: ngôn ngữ nguồn
    m_srcLangCombo->addItem(QIcon(IconFactory::makeTranslateIcon(18, QColor("#cbd5e1"))), "Tự động nhận diện");
    m_srcLangCombo->addItem("🇬🇧  Tiếng Anh (EN)");
    m_srcLangCombo->addItem("🇯🇵  Tiếng Nhật (JA)");
    m_srcLangCombo->addItem("🇰🇷  Tiếng Hàn (KO)");
    m_srcLangCombo->addItem("🇨🇳  Tiếng Trung (ZH)");

    // Mock: ngôn ngữ đích
    m_dstLangCombo->addItem(QIcon(IconFactory::makeVietnamFlag(20, 14)), "Tiếng Việt");
    m_dstLangCombo->addItem("🇬🇧  Tiếng Anh (EN)");

    // Mock: profiles
    m_profileCombo->addItem(QIcon(IconFactory::makeProfileIcon(18, QColor("#60a5fa"))), "Elden Ring - Default");
    m_profileCombo->addItem(QIcon(IconFactory::makeProfileIcon(18, QColor("#60a5fa"))), "Genshin Impact - Quest");
    m_profileCombo->addItem(QIcon(IconFactory::makeProfileIcon(18, QColor("#60a5fa"))), "Manga reader - Fullscreen");
}

void MainWindow::onTranslationStarted()
{
    m_statusIndicator->setState("Đang dịch...", QColor(StyleTheme::ColorSuccess));
}

void MainWindow::onTranslationStopped()
{
    m_statusIndicator->setState("Sẵn sàng", QColor(StyleTheme::ColorSuccess));
}
