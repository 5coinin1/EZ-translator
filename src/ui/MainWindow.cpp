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
#include "core/WindowEnumerator.h"
#include "platform/FramelessWindowHelper.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QMenu>
#include <QButtonGroup>
#include <QAction>
#include <QFrame>
#include <QCloseEvent>
#include <QPainter>

namespace {

/** Cặp mã ngôn ngữ (dùng cho engine dịch) ↔ nhãn hiển thị. */
struct LanguageOption
{
    const char* code;
    const char* label;
};

constexpr LanguageOption kSourceLanguages[] = {
    {"auto", "🌐  Tự động nhận diện"},
    {"en", "🇬🇧  Tiếng Anh (EN)"},
    {"ja", "🇯🇵  Tiếng Nhật (JA)"},
    {"ko", "🇰🇷  Tiếng Hàn (KO)"},
    {"zh", "🇨🇳  Tiếng Trung (ZH)"},
    {"vi", "🇻🇳  Tiếng Việt (VI)"},
};

constexpr LanguageOption kTargetLanguages[] = {
    {"vi", "🇻🇳  Tiếng Việt (VI)"},
    {"en", "🇬🇧  Tiếng Anh (EN)"},
    {"ja", "🇯🇵  Tiếng Nhật (JA)"},
    {"ko", "🇰🇷  Tiếng Hàn (KO)"},
    {"zh", "🇨🇳  Tiếng Trung (ZH)"},
    {"fr", "🇫🇷  Tiếng Pháp (FR)"},
    {"es", "🇪🇸  Tiếng Tây Ban Nha (ES)"},
    {"ru", "🇷🇺  Tiếng Nga (RU)"},
    {"th", "🇹🇭  Tiếng Thái (TH)"},
};

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QWidget(parent)
{
    setWindowTitle("EZ-Translator");
    setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
    setAttribute(Qt::WA_TranslucentBackground, false);
    setMinimumSize(500, 470);
    resize(520, 470);
    setStyleSheet(QString("QWidget#MainWindow { background-color: %1; border-radius: 12px; }")
                      .arg(StyleTheme::ColorBackground));
    setObjectName("MainWindow");
    buildUi();
    populateInitialData();
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

    // ── Thanh trên cùng: nút Cài đặt (đưa từ footer lên, dễ thấy hơn) ─────────
    auto* topRow = new QHBoxLayout();
    topRow->setContentsMargins(0, 0, 0, 0);
    topRow->addStretch();

    auto* settingsBtn = new QPushButton(QStringLiteral("⚙  Cài đặt"), content);
    settingsBtn->setFixedHeight(30);
    settingsBtn->setCursor(Qt::PointingHandCursor);
    settingsBtn->setToolTip(QStringLiteral("Mở cài đặt (engine dịch, API key, màu cửa sổ, …)"));
    settingsBtn->setStyleSheet(QStringLiteral(R"(
        QPushButton {
            background-color: #121a28;
            color: #cbd5e1;
            border: 1px solid #28374d;
            border-radius: 8px;
            font-size: 9pt;
            font-weight: 500;
            padding: 0 14px;
        }
        QPushButton:hover {
            background-color: #1a273b;
            border-color: #3b82f6;
            color: #ffffff;
        }
        QPushButton:pressed { background-color: #0f1622; }
    )"));
    connect(settingsBtn, &QPushButton::clicked, this, &MainWindow::requestOpenSettings);
    topRow->addWidget(settingsBtn);
    contentLay->addLayout(topRow);

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

    // Hoán đổi ngôn ngữ gốc ↔ đích (không áp dụng khi nguồn = tự nhận diện).
    connect(swapBtn, &IconButton::clicked, this, [this]() {
        const QString src = sourceLanguage();
        const QString dst = targetLanguage();
        if (src == QLatin1String("auto")) {
            setStatus("Không thể hoán đổi khi nguồn là Tự động nhận diện", QColor("#f59e0b"));
            return;
        }
        const int newSource = m_srcLangCombo->findData(dst);
        const int newTarget = m_dstLangCombo->findData(src);
        if (newSource < 0 || newTarget < 0)
            return;

        m_updatingLanguages = true;
        m_srcLangCombo->setCurrentIndex(newSource);
        m_dstLangCombo->setCurrentIndex(newTarget);
        m_updatingLanguages = false;
        emit languagesChanged(sourceLanguage(), targetLanguage());
    });

    langRow->addWidget(langBlock, 1);
    langRow->addWidget(swapWrapper);
    langRow->addWidget(dstBlock, 1);
    contentLay->addLayout(langRow);

    // ── Section 3: Hiển thị bản dịch ────────────────────────────────────────
    contentLay->addWidget(new SectionTitle(SectionIconType::Monitor, "3. Hiển thị bản dịch", content));

    // Segmented control: 2 lựa chọn loại trừ nhau, gọn 1 hàng thay cho 2 card lớn.
    // (Card lớn chiếm ~200px và tạo khoảng trống lớn phía trên nút Bắt đầu.)
    auto* displaySeg = new QWidget(content);
    displaySeg->setObjectName("displaySeg");
    displaySeg->setFixedHeight(44);
    displaySeg->setStyleSheet(R"(
        QWidget#displaySeg {
            background-color: #0b1320;
            border: 1px solid #243449;
            border-radius: 10px;
        }
        QPushButton {
            background: transparent;
            color: #94a3b8;
            border: none;
            border-radius: 8px;
            font-size: 9.5pt;
            font-weight: 600;
            padding: 0 14px;
        }
        QPushButton:hover { color: #e2e8f0; }
        QPushButton:checked { background-color: #1d4ed8; color: #ffffff; }
    )");

    auto* displaySegLay = new QHBoxLayout(displaySeg);
    displaySegLay->setContentsMargins(3, 3, 3, 3);
    displaySegLay->setSpacing(4);

    m_cardHover = new QPushButton(QStringLiteral("Dịch khi chỉ chuột"), displaySeg);
    m_cardHover->setCheckable(true);
    m_cardHover->setCursor(Qt::PointingHandCursor);
    m_cardHover->setToolTip(QStringLiteral("Chỉ chuột để dịch nhanh, hoặc kéo chuột trái bôi chọn chữ để dịch đoạn đã chọn bên phải."));
    m_cardFloat = new QPushButton(QStringLiteral("Cửa sổ nổi"), displaySeg);
    m_cardFloat->setCheckable(true);
    m_cardFloat->setCursor(Qt::PointingHandCursor);
    m_cardOverlay = new QPushButton(QStringLiteral("Overlay trên màn hình"), displaySeg);
    m_cardOverlay->setCheckable(true);
    m_cardOverlay->setCursor(Qt::PointingHandCursor);

    displaySegLay->addWidget(m_cardHover, 1);
    displaySegLay->addWidget(m_cardFloat, 1);
    displaySegLay->addWidget(m_cardOverlay, 1);

    auto* displayGroup = new QButtonGroup(this);
    displayGroup->setExclusive(true);
    displayGroup->addButton(m_cardHover, 0);
    displayGroup->addButton(m_cardFloat, 1);
    displayGroup->addButton(m_cardOverlay, 2);
    m_cardHover->setChecked(true);

    connect(displayGroup, &QButtonGroup::idToggled, this, [this](int id, bool checked) {
        // Dung idToggled (khong phai idClicked) de UI cung cap nhat khi trang thai
        // duoc set bang code/automation, khong chi khi bam chuot.
        if (!checked)
            return;
        const DisplayMode mode = id == 2   ? DisplayMode::ScreenOverlay
                                 : id == 1 ? DisplayMode::FloatingWindow
                                           : DisplayMode::HoverTranslate;
        applyDisplayModeUi(mode);
        emit displayModeChanged(mode);
    });

    contentLay->addWidget(displaySeg);

    // ── Section 4: Chọn vùng dịch ────────────────────────────────────────────
    // Cả section nằm trong một container để ẩn/hiện theo chế độ hiển thị: "Cửa sổ nổi"
    // không cần vùng dịch (hover tự dò quanh con trỏ), chỉ "Overlay trên màn hình" mới dùng.
    m_regionSection = new QWidget(content);
    auto* regionLay = new QVBoxLayout(m_regionSection);
    regionLay->setContentsMargins(0, 0, 0, 0);
    regionLay->setSpacing(StyleTheme::SpacingS);

    regionLay->addWidget(
        new SectionTitle(SectionIconType::Crop, "4. Chọn vùng dịch", m_regionSection));

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
            padding: 0 12px;
        }
        QPushButton:hover {
            background-color: #1a273b;
            border-color: #3b82f6;
            color: #ffffff;
        }
        QPushButton:pressed { background-color: #0f1622; }
    )";

    const QString menuStyle = R"(
        QMenu {
            background-color: #0e1724;
            border: 1px solid #28374d;
            border-radius: 8px;
            padding: 4px;
        }
        QMenu::item {
            color: #cbd5e1;
            padding: 7px 18px 7px 14px;
            border-radius: 6px;
            font-size: 9pt;
        }
        QMenu::item:selected { background-color: #1a273b; color: #ffffff; }
        QMenu::item:disabled { color: #55637a; }
        QMenu::separator { height: 1px; background: #243449; margin: 4px 8px; }
    )";

    // Một hàng duy nhất: chọn vùng · thêm vùng · menu thao tác.
    auto* regionRow = new QHBoxLayout();
    regionRow->setSpacing(StyleTheme::SpacingS);

    m_regionCombo = new AppComboBox(m_regionSection);
    m_regionCombo->setFixedHeight(38);
    m_regionCombo->setToolTip(QStringLiteral("Danh sách vùng dịch — chọn vùng để thao tác"));
    connect(m_regionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MainWindow::onRegionComboChanged);

    m_selectRegionBtn = new QPushButton(m_regionSection);
    m_selectRegionBtn->setIcon(QIcon(IconFactory::makeCropIcon(17, QColor("#38bdf8"))));
    m_selectRegionBtn->setText(QStringLiteral("  Thêm"));
    m_selectRegionBtn->setFixedHeight(38);
    m_selectRegionBtn->setToolTip(QStringLiteral("Khoanh thêm một vùng chữ trên màn hình"));
    m_selectRegionBtn->setStyleSheet(actionBtnStyle);
    connect(m_selectRegionBtn, &QPushButton::clicked, this, &MainWindow::onAddRegionClicked);

    m_regionMenuBtn = new QPushButton(QStringLiteral("⋯"), m_regionSection);
    m_regionMenuBtn->setFixedSize(40, 38);
    m_regionMenuBtn->setToolTip(
        QStringLiteral("Sửa / Xoá / Xem vùng / Preview ROI / Danh sách vùng"));
    m_regionMenuBtn->setStyleSheet(actionBtnStyle);

    m_regionMenu = new QMenu(m_regionMenuBtn);
    m_regionMenu->setStyleSheet(menuStyle);

    m_editRegionAction = m_regionMenu->addAction(QStringLiteral("Sửa vùng…"));
    connect(m_editRegionAction, &QAction::triggered, this, &MainWindow::onEditRegionClicked);

    m_showRegionAction = m_regionMenu->addAction(QStringLiteral("Xem vùng trên màn hình"));
    connect(m_showRegionAction, &QAction::triggered, this, &MainWindow::requestShowRegion);

    m_previewRoiAction = m_regionMenu->addAction(QStringLiteral("Preview ROI"));
    connect(m_previewRoiAction, &QAction::triggered, this, &MainWindow::requestPreviewRoi);

    m_removeRegionAction = m_regionMenu->addAction(QStringLiteral("Xoá vùng"));
    connect(m_removeRegionAction, &QAction::triggered, this, &MainWindow::onRemoveRegionClicked);

    m_regionMenu->addSeparator();
    m_manageRegionsAction = m_regionMenu->addAction(QStringLiteral("Danh sách vùng…"));
    connect(m_manageRegionsAction, &QAction::triggered, this, &MainWindow::onManageRegionsClicked);

    m_regionMenuBtn->setMenu(m_regionMenu);

    regionRow->addWidget(m_regionCombo, 1);
    regionRow->addWidget(m_selectRegionBtn);
    regionRow->addWidget(m_regionMenuBtn);
    regionLay->addLayout(regionRow);

    // Một dòng thông tin thay cho khung 54px trước đây.
    m_regionHintLabel = new QLabel(m_regionSection);
    m_regionHintLabel->setWordWrap(true);
    m_regionHintLabel->setStyleSheet(
        QStringLiteral("color:#64748b; font-size:8pt; border:none; background:transparent;"));
    regionLay->addWidget(m_regionHintLabel);

    contentLay->addWidget(m_regionSection);
    updateRegionDisplay();
    // Mặc định là "Cửa sổ nổi" ⇒ Section chọn vùng ẩn cho tới khi người dùng chọn Overlay.
    applyDisplayModeUi(displayMode());

    contentLay->addStretch();

    // ── Nút Bắt đầu dịch ──────────────────────────────────────────────────────
    m_startBtn = new PrimaryButton(QIcon(IconFactory::makePlayIcon(18, Qt::white)),
                                   QString("  Bắt đầu dịch (%1)").arg(m_startStopHotkey), content);
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

    // Phím tắt bàn phím: đăng ký TOÀN CỤC ở AppController (GlobalHotkeys) để dùng được
    // khi cửa sổ game đang focus; ở đây chỉ còn phần hiển thị (setHotkeyHints).

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

    root->addWidget(footer);
}

void MainWindow::populateInitialData()
{
    // Quét danh sách các cửa sổ đang chạy thực tế trên máy
    refreshWindowList();

    m_updatingLanguages = true;
    m_srcLangCombo->clear();
    for (const LanguageOption& language : kSourceLanguages) {
        const QString code = QString::fromLatin1(language.code);
        if (code == QLatin1String("auto")) {
            m_srcLangCombo->addItem(
                QIcon(IconFactory::makeTranslateIcon(18, QColor("#cbd5e1"))),
                QString::fromUtf8(language.label), code);
        } else {
            m_srcLangCombo->addItem(QString::fromUtf8(language.label), code);
        }
    }

    m_dstLangCombo->clear();
    for (const LanguageOption& language : kTargetLanguages) {
        const QString code = QString::fromLatin1(language.code);
        if (code == QLatin1String("vi")) {
            m_dstLangCombo->addItem(
                QIcon(IconFactory::makeVietnamFlag(20, 14)),
                QString::fromUtf8(language.label), code);
        } else {
            m_dstLangCombo->addItem(QString::fromUtf8(language.label), code);
        }
    }
    m_updatingLanguages = false;

    const auto notify = [this]() {
        if (m_updatingLanguages)
            return;
        const QString source = sourceLanguage();
        const QString target = targetLanguage();
        if (!source.isEmpty() && !target.isEmpty())
            emit languagesChanged(source, target);
    };

    connect(m_srcLangCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [notify](int) { notify(); });
    connect(m_dstLangCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [notify](int) { notify(); });
}

void MainWindow::setLanguages(const QString& source, const QString& target)
{
    m_updatingLanguages = true;

    int sourceIndex = m_srcLangCombo->findData(source);
    if (sourceIndex < 0)
        sourceIndex = 0; // Tự động nhận diện
    m_srcLangCombo->setCurrentIndex(sourceIndex);

    int targetIndex = m_dstLangCombo->findData(target);
    if (targetIndex < 0)
        targetIndex = 0; // Tiếng Việt
    m_dstLangCombo->setCurrentIndex(targetIndex);

    m_updatingLanguages = false;
}

QString MainWindow::sourceLanguage() const
{
    return m_srcLangCombo->currentData().toString();
}

QString MainWindow::targetLanguage() const
{
    return m_dstLangCombo->currentData().toString();
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

void MainWindow::applyDisplayModeUi(DisplayMode mode)
{
    if (!m_regionSection)
        return;

    // "Cửa sổ nổi" = bản dịch hiện trong popup cạnh con trỏ ⇒ không có khái niệm vùng dịch
    // để chọn. "Overlay trên màn hình" mới cần khoanh vùng để biết chỗ vẽ bản dịch.
    // Hover không cần vùng dịch. Cửa sổ nổi và Overlay đều cho người dùng
    // khoanh vùng/layout để biết chỗ nào cần OCR và dịch.
    m_regionSection->setVisible(mode != DisplayMode::HoverTranslate);
}

void MainWindow::updateRegionDisplay()
{
    if (!m_regionHintLabel)
        return;

    const bool valid =
        m_hasRegion
        && (m_currentRegion.isValid()
            || (m_currentScreenRect.isValid() && m_currentScreenRect.width() >= 10
                && m_currentScreenRect.height() >= 10));

    if (!valid) {
        m_regionHintLabel->setText(
            QStringLiteral("Chưa có vùng — bấm ＋ Thêm để khoanh vùng trên màn hình."));
        return;
    }

    QString detail;
    if (m_currentScreenRect.isValid() && m_currentScreenRect.width() > 0) {
        detail = QStringLiteral("%1 × %2 px · tại (%3, %4)")
                     .arg(m_currentScreenRect.width())
                     .arg(m_currentScreenRect.height())
                     .arg(m_currentScreenRect.x())
                     .arg(m_currentScreenRect.y());
    } else {
        detail = QStringLiteral("%1% × %2% · lệch trái %3%, đỉnh %4%")
                     .arg(qRound(m_currentRegion.width * 100.0))
                     .arg(qRound(m_currentRegion.height * 100.0))
                     .arg(qRound(m_currentRegion.x * 100.0))
                     .arg(qRound(m_currentRegion.y * 100.0));
    }
    m_regionHintLabel->setText(QStringLiteral("Vùng đang chọn: ") + detail);
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
    if (m_cardOverlay && m_cardOverlay->isChecked())
        return DisplayMode::ScreenOverlay;
    if (m_cardFloat && m_cardFloat->isChecked())
        return DisplayMode::FloatingWindow;
    return DisplayMode::HoverTranslate;
}

void MainWindow::setDisplayMode(DisplayMode mode)
{
    QPushButton* button = mode == DisplayMode::ScreenOverlay ? m_cardOverlay
                          : mode == DisplayMode::FloatingWindow ? m_cardFloat
                                                                 : m_cardHover;
    if (button && !button->isChecked())
        button->setChecked(true); // idToggled đồng bộ AppController và UI
}

void MainWindow::setRegions(const QList<EZTranslator::TranslationRegion>& regions, int current)
{
    if (!m_regionCombo)
        return;

    m_updatingRegions = true;
    m_regionCombo->clear();

    if (regions.isEmpty()) {
        m_regionCombo->addItem(QStringLiteral("Toàn bộ cửa sổ (chưa có vùng riêng)"), -1);
        m_regionCombo->setEnabled(false);
    } else {
        for (int i = 0; i < regions.size(); ++i) {
            const EZTranslator::TranslationRegion& region = regions.at(i);
            const EZTranslator::NormalizedRect& r = region.normalizedRect;
            const QString label =
                QStringLiteral("%1  ·  %2% × %3%")
                    .arg(region.name.isEmpty()
                             ? QStringLiteral("Vùng dịch %1").arg(i + 1)
                             : region.name)
                    .arg(qRound(r.width * 100.0))
                    .arg(qRound(r.height * 100.0));
            m_regionCombo->addItem(label, i);
        }
        const int target = (current >= 0 && current < regions.size()) ? current : 0;
        m_regionCombo->setCurrentIndex(target);
        m_regionCombo->setEnabled(true);
    }
    m_updatingRegions = false;

    const bool hasSelection = !regions.isEmpty();
    for (QAction* action : {m_editRegionAction, m_removeRegionAction, m_showRegionAction,
                            m_previewRoiAction}) {
        if (action)
            action->setEnabled(hasSelection);
    }
}

int MainWindow::currentRegionIndex() const
{
    if (!m_regionCombo)
        return -1;
    return m_regionCombo->currentData().toInt();
}

void MainWindow::onRegionComboChanged(int /*index*/)
{
    if (m_updatingRegions)
        return;
    emit regionSelected(currentRegionIndex());
}

void MainWindow::onAddRegionClicked()
{
    if (selectedWindowHandle() == 0) {
        showSelectWindowWarning();
        return;
    }
    emit requestAddRegion();
}

void MainWindow::onEditRegionClicked()
{
    if (selectedWindowHandle() == 0) {
        showSelectWindowWarning();
        return;
    }
    const int index = currentRegionIndex();
    if (index < 0) {
        m_statusIndicator->setState(QStringLiteral("Chưa có vùng nào để sửa — bấm \"Thêm vùng\" trước"),
                                    QColor(StyleTheme::ColorWarning));
        return;
    }
    emit requestEditRegion(index);
}

void MainWindow::onRemoveRegionClicked()
{
    const int index = currentRegionIndex();
    if (index < 0) {
        m_statusIndicator->setState(QStringLiteral("Chưa có vùng nào để xoá"),
                                    QColor(StyleTheme::ColorTextSecondary));
        return;
    }
    emit requestRemoveRegion(index);
}

void MainWindow::onManageRegionsClicked()
{
    if (selectedWindowHandle() == 0) {
        showSelectWindowWarning();
        return;
    }
    emit requestOpenRegionList();
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
        m_startBtn->setText(QString("  Đang dịch (%1 để dừng)").arg(m_startStopHotkey));
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
        m_startBtn->setText(QString("  Bắt đầu dịch (%1)").arg(m_startStopHotkey));
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
    if (m_showRegionAction)
        m_showRegionAction->setText(active ? QStringLiteral("Ẩn khung vùng")
                                           : QStringLiteral("Xem vùng trên màn hình"));
}

void MainWindow::setHotkeyHints(const QString& startStopKey, const QString& selectRegionKey)
{
    m_startStopHotkey = startStopKey.isEmpty() ? QStringLiteral("(chưa gán)") : startStopKey;
    m_selectRegionHotkey = selectRegionKey.isEmpty() ? QStringLiteral("(chưa gán)") : selectRegionKey;

    if (m_startBtn)
        m_startBtn->setText(m_isTranslating
                                ? QString("  Đang dịch (%1 để dừng)").arg(m_startStopHotkey)
                                : QString("  Bắt đầu dịch (%1)").arg(m_startStopHotkey));
    updateRegionDisplay();
}

void MainWindow::setCloseToTray(bool enabled)
{
    m_closeToTray = enabled;
}

bool MainWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
    // Frameless window: bật resize bằng viền (kéo cửa sổ do TitleBar đảm nhiệm).
    if (EZTranslator::FramelessWindow::handleNativeMessage(this, eventType, message, result))
        return true;
    return QWidget::nativeEvent(eventType, message, result);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    // "Thu nhỏ xuống khay khi đóng" (Cài đặt → Chung): giữ app + pipeline dịch chạy nền.
    if (m_closeToTray) {
        event->ignore();
        hide();
        return;
    }
    QWidget::closeEvent(event);
}

