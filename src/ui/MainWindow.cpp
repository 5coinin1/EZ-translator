#include "MainWindow.h"
#include "ui/theme/StyleTheme.h"
#include "ui/components/TitleBar.h"
#include "ui/components/SectionTitle.h"
#include "ui/components/AppComboBox.h"
#include "ui/components/PrimaryButton.h"
#include "ui/components/SecondaryButton.h"
#include "ui/components/IconButton.h"
#include "ui/components/StatusIndicator.h"
#include "ui/components/DisplayModeCard.h"
#include "ui/theme/IconFactory.h"
#include "core/WindowEnumerator.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QShortcut>
#include <QPainter>

// ── Widget vẽ thumbnail mini hiển thị trực quan vị trí vùng dịch trên cửa sổ ────
class RegionThumbnailWidget : public QWidget
{
public:
    explicit RegionThumbnailWidget(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setFixedSize(44, 32);
    }

    void setRegion(bool hasRegion, const EZTranslator::NormalizedRect& rect)
    {
        m_hasRegion = hasRegion;
        m_rect = rect;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        // Khung mô phỏng cửa sổ đích
        QRectF winRect(1.5, 1.5, width() - 3.0, height() - 3.0);
        QColor borderCol = m_hasRegion ? QColor("#3b82f6") : QColor("#334155");
        QColor bgCol = m_hasRegion ? QColor("#0c192e") : QColor("#0d1522");

        p.setPen(QPen(borderCol, 1.2f));
        p.setBrush(bgCol);
        p.drawRoundedRect(winRect, 3, 3);

        if (m_hasRegion && m_rect.isValid()) {
            // Vùng chọn bên trong cửa sổ
            float innerX = winRect.x() + 2;
            float innerY = winRect.y() + 2;
            float innerW = winRect.width() - 4;
            float innerH = winRect.height() - 4;

            float rx = innerX + float(m_rect.x) * innerW;
            float ry = innerY + float(m_rect.y) * innerH;
            float rw = qMax(6.0f, float(m_rect.width) * innerW);
            float rh = qMax(6.0f, float(m_rect.height) * innerH);

            rx = qBound(innerX, rx, innerX + innerW - rw);
            ry = qBound(innerY, ry, innerY + innerH - rh);

            QRectF selRect(rx, ry, rw, rh);
            p.setPen(QPen(QColor("#38bdf8"), 1.2f));
            p.setBrush(QColor(56, 189, 248, 120));
            p.drawRoundedRect(selRect, 2, 2);
        } else {
            // Biểu tượng toàn màn hình (khung nét đứt mờ)
            QPen dash(QColor("#475569"), 1.0f, Qt::DashLine);
            p.setPen(dash);
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(winRect.adjusted(3, 3, -3, -3), 2, 2);
        }
    }

private:
    bool m_hasRegion{false};
    EZTranslator::NormalizedRect m_rect;
};

MainWindow::MainWindow(QWidget* parent)
    : QWidget(parent)
{
    setWindowTitle("EZ-Translator");
    setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
    setAttribute(Qt::WA_TranslucentBackground, false);
    setMinimumSize(500, 720);
    resize(520, 750);
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
    root->setSizeConstraint(QLayout::SetMinimumSize);

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
    contentLay->setContentsMargins(20, 14, 20, 14);
    contentLay->setSpacing(12);
    contentLay->setSizeConstraint(QLayout::SetMinimumSize);

    // ── Section 1: Chọn cửa sổ ───────────────────────────────────────────────
    contentLay->addWidget(new SectionTitle(SectionIconType::Gamepad, "1. Chọn cửa sổ cần dịch", content));

    auto* windowRow = new QHBoxLayout();
    windowRow->setSpacing(StyleTheme::SpacingS);
    m_windowCombo = new AppComboBox(content);
    m_windowCombo->setIconSize(QSize(20, 20));
    connect(m_windowCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onWindowIndexChanged);

    auto* refreshBtn = new IconButton("⟳", content);
    refreshBtn->setToolTip("Làm mới danh sách cửa sổ");
    connect(refreshBtn, &IconButton::clicked, this, &MainWindow::refreshWindowList);

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

    // Wrap swapBtn trong container có spacer trên đầu để căn giữa với combobox
    auto* swapWrapper = new QWidget(content);
    auto* swapWrapLay = new QVBoxLayout(swapWrapper);
    swapWrapLay->setContentsMargins(0, 0, 0, 0);
    swapWrapLay->setSpacing(0);

    auto* swapBtn = new IconButton("⇄", swapWrapper);
    swapBtn->setToolTip("Hoán đổi ngôn ngữ");

    swapWrapLay->addStretch();   // Đẩy nút xuống căn đáy (ngang bằng combobox)
    swapWrapLay->addWidget(swapBtn);

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
    langRow->addWidget(swapWrapper);
    langRow->addWidget(dstBlock, 1);
    contentLay->addLayout(langRow);

    // ── Section 3: Hiển thị bản dịch ────────────────────────────────────────
    contentLay->addWidget(new SectionTitle(SectionIconType::Monitor, "3. Hiển thị bản dịch", content));

    auto* displayRow = new QHBoxLayout();
    displayRow->setSpacing(StyleTheme::SpacingM);

    m_cardFloat = new DisplayModeCard(
        IconFactory::makeMonitorIcon(32, QColor("#64748b")),
        IconFactory::makeMonitorIcon(32, QColor("#60a5fa")),
        "Cửa sổ nổi",
        "Hiển thị trong cửa sổ\ncủa ứng dụng",
        content);

    m_cardOverlay = new DisplayModeCard(
        IconFactory::makeDashedOverlayIcon(32, QColor("#64748b")),
        IconFactory::makeDashedOverlayIcon(32, QColor("#60a5fa")),
        "Overlay trên màn hình",
        "Hiển thị trực tiếp\nlên ứng dụng/game",
        content);

    m_cardFloat->setSelected(true);
    m_cardOverlay->setSelected(false);

    connect(m_cardFloat, &DisplayModeCard::clicked, this, [this]() {
        m_cardFloat->setSelected(true);
        m_cardOverlay->setSelected(false);
        emit displayModeChanged(DisplayMode::FloatingWindow);
    });

    connect(m_cardOverlay, &DisplayModeCard::clicked, this, [this]() {
        m_cardFloat->setSelected(false);
        m_cardOverlay->setSelected(true);
        emit displayModeChanged(DisplayMode::ScreenOverlay);
    });

    displayRow->addWidget(m_cardFloat, 1);
    displayRow->addWidget(m_cardOverlay, 1);
    contentLay->addLayout(displayRow);

    // ── Section 4: Chọn vùng dịch ────────────────────────────────────────────
    contentLay->addWidget(new SectionTitle(SectionIconType::Crop, "4. Chọn vùng dịch", content));

    auto* regionBtnRow = new QHBoxLayout();
    regionBtnRow->setSpacing(StyleTheme::SpacingS);

    const QString actionBtnStyle = R"(
        QPushButton {
            background-color: #121a28;
            color: #cbd5e1;
            border: 1px solid #28374d;
            border-radius: 8px;
            font-size: 9pt;
            font-weight: 500;
            padding: 0 8px;
        }
        QPushButton:hover {
            background-color: #1a273b;
            border-color: #3b82f6;
            color: #ffffff;
        }
        QPushButton:pressed {
            background-color: #0f1622;
        }
    )";

    const QString deleteBtnStyle = R"(
        QPushButton {
            background-color: #121a28;
            color: #cbd5e1;
            border: 1px solid #28374d;
            border-radius: 8px;
            font-size: 9pt;
            font-weight: 500;
            padding: 0 8px;
        }
        QPushButton:hover {
            background-color: #24171e;
            border-color: #ef4444;
            color: #ef4444;
        }
        QPushButton:pressed {
            background-color: #191015;
        }
    )";

    m_selectRegionBtn = new QPushButton(content);
    m_selectRegionBtn->setIcon(QIcon(IconFactory::makeCropIcon(17, QColor("#38bdf8"))));
    m_selectRegionBtn->setText("  Chọn vùng (F6)");
    m_selectRegionBtn->setFixedHeight(38);
    m_selectRegionBtn->setToolTip("Dùng chuột kéo chọn vùng chữ cần dịch trên màn hình (phím tắt F6)");
    m_selectRegionBtn->setStyleSheet(actionBtnStyle);

    m_showRegionBtn = new QPushButton(content);
    m_showRegionBtn->setIcon(QIcon(IconFactory::makeEyeIcon(16, QColor("#38bdf8"))));
    m_showRegionBtn->setText("  Xem vùng");
    m_showRegionBtn->setFixedHeight(38);
    m_showRegionBtn->setToolTip("Hiển thị khung đỏ tại vị trí vùng dịch đã chọn trên màn hình");
    m_showRegionBtn->setStyleSheet(actionBtnStyle);

    m_clearRegionBtn = new QPushButton(content);
    m_clearRegionBtn->setIcon(QIcon(IconFactory::makeTrashIcon(16, QColor("#94a3b8"))));
    m_clearRegionBtn->setText("  Xóa vùng");
    m_clearRegionBtn->setFixedHeight(38);
    m_clearRegionBtn->setToolTip("Xóa vùng dịch đã chọn và quay về chế độ dịch toàn bộ cửa sổ");
    m_clearRegionBtn->setStyleSheet(deleteBtnStyle);

    connect(m_selectRegionBtn, &QPushButton::clicked, this, &MainWindow::onSelectRegionClicked);
    connect(m_showRegionBtn,   &QPushButton::clicked, this, &MainWindow::requestShowRegion);
    connect(m_clearRegionBtn,  &QPushButton::clicked, this, &MainWindow::clearRegion);

    regionBtnRow->addWidget(m_selectRegionBtn, 4);
    regionBtnRow->addWidget(m_showRegionBtn, 3);
    regionBtnRow->addWidget(m_clearRegionBtn, 3);
    contentLay->addLayout(regionBtnRow);

    // Hiển thị thông tin vùng dịch đã chọn
    m_regionDisplayFrame = new QFrame(content);
    m_regionDisplayFrame->setObjectName("regionDisplayFrame");
    m_regionDisplayFrame->setFixedHeight(54);

    auto* rdispLay = new QHBoxLayout(m_regionDisplayFrame);
    rdispLay->setContentsMargins(12, 6, 12, 6);
    rdispLay->setSpacing(12);

    m_regionThumbnail = new RegionThumbnailWidget(m_regionDisplayFrame);

    auto* textContainer = new QVBoxLayout();
    textContainer->setContentsMargins(0, 0, 0, 0);
    textContainer->setSpacing(3);

    m_regionTitleLabel = new QLabel(m_regionDisplayFrame);
    m_regionDetailLabel = new QLabel(m_regionDisplayFrame);

    textContainer->addWidget(m_regionTitleLabel);
    textContainer->addWidget(m_regionDetailLabel);

    m_regionBadgeLabel = new QLabel(m_regionDisplayFrame);
    m_regionBadgeLabel->setAlignment(Qt::AlignCenter);

    rdispLay->addWidget(m_regionThumbnail);
    rdispLay->addLayout(textContainer, 1);
    rdispLay->addWidget(m_regionBadgeLabel);

    contentLay->addWidget(m_regionDisplayFrame);
    updateRegionDisplay();

    contentLay->addStretch();

    // ── Nút Bắt đầu dịch ──────────────────────────────────────────────────────
    m_startBtn = new PrimaryButton(QIcon(IconFactory::makePlayIcon(18, Qt::white)), "  Bắt đầu dịch (F8)", content);
    m_startBtn->setFixedHeight(46);
    m_startBtn->setStyleSheet(R"(
        QPushButton {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #2563eb, stop:1 #3b82f6);
            color: #ffffff;
            border: none;
            border-radius: 10px;
            font-size: 10.5pt;
            font-weight: 600;
            padding: 0 20px;
        }
        QPushButton:hover {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #1d4ed8, stop:1 #2563eb);
        }
        QPushButton:pressed {
            background: #1e40af;
        }
    )");
    connect(m_startBtn, &QPushButton::clicked, this, &MainWindow::onStartButtonClicked);
    contentLay->addWidget(m_startBtn);

    // Phím tắt bàn phím tiện lợi
    auto* f6 = new QShortcut(QKeySequence(Qt::Key_F6), this);
    connect(f6, &QShortcut::activated, this, &MainWindow::onSelectRegionClicked);

    auto* f8 = new QShortcut(QKeySequence(Qt::Key_F8), this);
    connect(f8, &QShortcut::activated, this, &MainWindow::onStartButtonClicked);

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

    footerLay->addWidget(m_statusIndicator);
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
    // Quét danh sách các cửa sổ đang chạy thực tế trên máy
    refreshWindowList();

    // Mock: ngôn ngữ nguồn
    m_srcLangCombo->addItem(QIcon(IconFactory::makeTranslateIcon(18, QColor("#cbd5e1"))), "Tự động nhận diện");
    m_srcLangCombo->addItem("🇬🇧  Tiếng Anh (EN)");
    m_srcLangCombo->addItem("🇯🇵  Tiếng Nhật (JA)");
    m_srcLangCombo->addItem("🇰🇷  Tiếng Hàn (KO)");
    m_srcLangCombo->addItem("🇨🇳  Tiếng Trung (ZH)");

    // Mock: ngôn ngữ đích
    m_dstLangCombo->addItem(QIcon(IconFactory::makeVietnamFlag(20, 14)), "Tiếng Việt");
    m_dstLangCombo->addItem("🇬🇧  Tiếng Anh (EN)");
}

quintptr MainWindow::selectedWindowHandle() const
{
    if (m_windowCombo->currentIndex() < 0) return 0;
    return m_windowCombo->currentData().value<quintptr>();
}

void MainWindow::onWindowIndexChanged(int index)
{
    if (index < 0 || index >= m_windowCombo->count()) {
        emit targetWindowSelected(0, QString(), QString());
        return;
    }
    quintptr handle = m_windowCombo->itemData(index).value<quintptr>();
    QString title = m_windowCombo->itemText(index);
    emit targetWindowSelected(handle, title, QString());
}

void MainWindow::refreshWindowList()
{
    quintptr currentHandle = selectedWindowHandle();

    m_windowCombo->blockSignals(true);
    m_windowCombo->clear();

    const auto windows = EZTranslator::WindowEnumerator::enumerateWindows();
    int restoreIndex = -1;

    for (int i = 0; i < windows.size(); ++i) {
        const auto& win = windows.at(i);
        QString text = win.title;
        if (!win.processName.isEmpty()) {
            text += QString(" (%1)").arg(win.processName);
        }

        QIcon icon = win.icon.isNull() ? QIcon(IconFactory::makeGameThumbnailIcon(20)) : win.icon;
        m_windowCombo->addItem(icon, text, QVariant::fromValue(win.handle));
        m_windowCombo->setItemData(i, QString("Tiêu đề: %1\nTiến trình: %2").arg(win.title, win.processName), Qt::ToolTipRole);

        if (currentHandle != 0 && win.handle == currentHandle) {
            restoreIndex = i;
        }
    }

    m_windowCombo->setPlaceholderText("-- Chọn cửa sổ cần dịch --");
    if (windows.isEmpty()) {
        m_windowCombo->addItem(QIcon(IconFactory::makeGameThumbnailIcon(20)), "Không tìm thấy cửa sổ ứng dụng nào");
        m_windowCombo->setCurrentIndex(-1);
    } else {
        if (restoreIndex >= 0) {
            m_windowCombo->setCurrentIndex(restoreIndex);
        } else {
            m_windowCombo->setCurrentIndex(-1);
        }
    }

    m_windowCombo->blockSignals(false);
    onWindowIndexChanged(m_windowCombo->currentIndex());
}

void MainWindow::updateRegionDisplay()
{
    bool valid = m_hasRegion && (m_currentRegion.isValid() || (m_currentScreenRect.isValid() && m_currentScreenRect.width() >= 10 && m_currentScreenRect.height() >= 10));

    if (!valid) {
        m_regionThumbnail->setRegion(false, {});

        m_regionDisplayFrame->setStyleSheet(R"(
            QFrame#regionDisplayFrame {
                background-color: #0b1320;
                border: 1px dashed #243449;
                border-radius: 8px;
            }
        )");

        m_regionTitleLabel->setText("Chế độ: Dịch toàn bộ cửa sổ");
        m_regionTitleLabel->setStyleSheet("color: #e2e8f0; font-size: 9pt; font-weight: 600; border: none; background: transparent;");

        m_regionDetailLabel->setText("Chưa khoanh vùng riêng — Hệ thống sẽ tự động quét chữ trên toàn bộ cửa sổ");
        m_regionDetailLabel->setStyleSheet("color: #64748b; font-size: 8pt; border: none; background: transparent;");

        m_regionBadgeLabel->setText("TOÀN CỬA SỔ");
        m_regionBadgeLabel->setStyleSheet("color: #94a3b8; background-color: #141f30; border-radius: 4px; padding: 3px 8px; font-size: 7.5pt; font-weight: 600; border: none;");
    } else {
        m_regionThumbnail->setRegion(true, m_currentRegion);

        m_regionDisplayFrame->setStyleSheet(R"(
            QFrame#regionDisplayFrame {
                background-color: #0d1e38;
                border: 1.5px solid #2563eb;
                border-radius: 8px;
            }
        )");

        if (m_currentScreenRect.isValid() && m_currentScreenRect.width() > 0 && m_currentScreenRect.height() > 0) {
            m_regionTitleLabel->setText(QString("Đã khoanh 1 vùng dịch riêng (%1 × %2 px)")
                                            .arg(m_currentScreenRect.width())
                                            .arg(m_currentScreenRect.height()));
            m_regionDetailLabel->setText(QString("Vị trí màn hình: X: %1, Y: %2  •  Bấm F6 để chọn lại hoặc \"Xóa vùng\" để hủy")
                                             .arg(m_currentScreenRect.x())
                                             .arg(m_currentScreenRect.y()));
        } else {
            int pctX = qRound(m_currentRegion.x * 100.0);
            int pctY = qRound(m_currentRegion.y * 100.0);
            int pctW = qRound(m_currentRegion.width * 100.0);
            int pctH = qRound(m_currentRegion.height * 100.0);

            m_regionTitleLabel->setText(QString("Đã khoanh 1 vùng dịch riêng (%1% × %2%)").arg(pctW).arg(pctH));
            m_regionDetailLabel->setText(QString("Vị trí: Lệch trái %1%, đỉnh %2%  •  Bấm F6 để chọn lại hoặc \"Xóa vùng\" để hủy")
                                             .arg(pctX).arg(pctY));
        }

        m_regionTitleLabel->setStyleSheet("color: #60a5fa; font-size: 9pt; font-weight: 600; border: none; background: transparent;");
        m_regionDetailLabel->setStyleSheet("color: #cbd5e1; font-size: 8pt; border: none; background: transparent;");

        m_regionBadgeLabel->setText("✓ ĐANG ÁP DỤNG");
        m_regionBadgeLabel->setStyleSheet("color: #38bdf8; background-color: #1e3a8a; border-radius: 4px; padding: 3px 8px; font-size: 7.5pt; font-weight: bold; border: none;");
    }
}

void MainWindow::setRegion(const EZTranslator::NormalizedRect& rect, const QRect& screenRect)
{
    m_hasRegion = rect.isValid() || (screenRect.isValid() && screenRect.width() >= 10 && screenRect.height() >= 10);
    m_currentRegion = rect;
    m_currentScreenRect = screenRect;
    updateRegionDisplay();
    m_statusIndicator->setState("Đã lưu vùng dịch thành công!", QColor(StyleTheme::ColorSuccess));
}

void MainWindow::clearRegion()
{
    m_hasRegion = false;
    m_currentRegion = {};
    m_currentScreenRect = {};
    updateRegionDisplay();
    m_statusIndicator->setState("Đã xóa vùng (sẽ dịch toàn bộ cửa sổ)", QColor(StyleTheme::ColorTextSecondary));
    emit requestClearRegion();
}

MainWindow::DisplayMode MainWindow::displayMode() const
{
    if (m_cardOverlay && m_cardOverlay->isSelected()) {
        return DisplayMode::ScreenOverlay;
    }
    return DisplayMode::FloatingWindow;
}

void MainWindow::onSelectRegionClicked()
{
    if (selectedWindowHandle() == 0) {
        showSelectWindowWarning();
        return;
    }
    emit requestOpenRegionEditor();
}

void MainWindow::onStartButtonClicked()
{
    if (m_isTranslating) {
        emit requestStopTranslation();
        return;
    }

    if (selectedWindowHandle() == 0) {
        showSelectWindowWarning();
        return;
    }

    emit requestStartTranslation();
}

void MainWindow::onTranslationStarted()
{
    m_isTranslating = true;
    m_statusIndicator->setState("Đang dịch...", QColor(StyleTheme::ColorSuccess));

    if (m_startBtn) {
        m_startBtn->setText("  Đang dịch (F8 để dừng)");
        m_startBtn->setIcon(QIcon(IconFactory::makeStopIcon(16, Qt::white)));
        m_startBtn->setStyleSheet(R"(
            QPushButton {
                background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #059669, stop:1 #10b981);
                color: #ffffff;
                border: none;
                border-radius: 10px;
                font-size: 10.5pt;
                font-weight: 600;
                padding: 0 20px;
            }
            QPushButton:hover {
                background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #047857, stop:1 #059669);
            }
            QPushButton:pressed {
                background: #065f46;
            }
        )");
    }
}

void MainWindow::onTranslationStopped()
{
    m_isTranslating = false;
    m_statusIndicator->setState("Sẵn sàng", QColor(StyleTheme::ColorSuccess));

    if (m_startBtn) {
        m_startBtn->setText("  Bắt đầu dịch (F8)");
        m_startBtn->setIcon(QIcon(IconFactory::makePlayIcon(18, Qt::white)));
        m_startBtn->setStyleSheet(R"(
            QPushButton {
                background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #2563eb, stop:1 #3b82f6);
                color: #ffffff;
                border: none;
                border-radius: 10px;
                font-size: 10.5pt;
                font-weight: 600;
                padding: 0 20px;
            }
            QPushButton:hover {
                background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #1d4ed8, stop:1 #2563eb);
            }
            QPushButton:pressed {
                background: #1e40af;
            }
        )");
    }
}

void MainWindow::showSelectWindowWarning()
{
    m_statusIndicator->setState("Vui lòng chọn cửa sổ cần dịch trước!", QColor(StyleTheme::ColorWarning));
    m_windowCombo->setFocus();
    m_windowCombo->showPopup();
}

void MainWindow::showRegionSelectedSuccess()
{
    m_statusIndicator->setState("Đã chọn vùng dịch!", QColor(StyleTheme::ColorSuccess));
}

void MainWindow::setStatus(const QString& text, const QColor& color)
{
    QColor c = color.isValid() ? color : QColor(StyleTheme::ColorTextSecondary);
    m_statusIndicator->setState(text, c);
}

void MainWindow::setShowRegionActive(bool active)
{
    if (active) {
        m_showRegionBtn->setText("  Ẩn vùng");
        m_showRegionBtn->setStyleSheet(R"(
            QPushButton {
                background-color: #1e1b4b;
                color: #f87171;
                border: 1.5px solid #ef4444;
                border-radius: 8px;
                font-size: 9pt;
                font-weight: 600;
                padding: 0 10px;
            }
            QPushButton:hover {
                background-color: #2e1065;
                border-color: #f87171;
            }
        )");
    } else {
        m_showRegionBtn->setText("  Xem vùng");
        m_showRegionBtn->setStyleSheet(R"(
            QPushButton {
                background-color: #121a28;
                color: #93c5fd;
                border: 1px solid #1e3a8a;
                border-radius: 8px;
                font-size: 9pt;
                font-weight: 500;
                padding: 0 10px;
            }
            QPushButton:hover {
                background-color: #1e293b;
                border-color: #3b82f6;
                color: #ffffff;
            }
            QPushButton:pressed {
                background-color: #0f172a;
            }
        )");
    }
}

