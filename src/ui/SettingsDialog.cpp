#include "SettingsDialog.h"
#include "ui/theme/StyleTheme.h"
#include "ui/components/PrimaryButton.h"
#include "ui/components/SecondaryButton.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QStackedWidget>
#include <QLabel>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QSlider>
#include <QPushButton>
#include <QFrame>
#include <QFormLayout>
#include <QGroupBox>

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::Dialog);
    setMinimumSize(700, 520);
    resize(760, 560);
    setStyleSheet(QString("background-color: %1; border-radius: 12px;")
                      .arg(StyleTheme::ColorBackground));
    buildUi();
}

// ─── Helper to create a page heading ─────────────────────────────────────────
static QLabel* makePageTitle(const QString& icon, const QString& title)
{
    auto* l = new QLabel(icon + "  " + title);
    l->setStyleSheet(QString("color: %1; font-size: 14pt; font-weight: 600;")
                         .arg(StyleTheme::ColorTextPrimary));
    return l;
}

static QFrame* makeDivider()
{
    auto* f = new QFrame();
    f->setFixedHeight(1);
    f->setStyleSheet(QString("background: %1;").arg(StyleTheme::ColorBorder));
    return f;
}

void SettingsDialog::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Dialog title bar ──────────────────────────────────────────────────────
    auto* titleBar = new QWidget(this);
    titleBar->setFixedHeight(50);
    titleBar->setStyleSheet(QString("background: %1; border-bottom: 1px solid %2;")
                                .arg(StyleTheme::ColorBackground).arg(StyleTheme::ColorBorder));
    auto* tbLay = new QHBoxLayout(titleBar);
    tbLay->setContentsMargins(20, 0, 16, 0);
    auto* tbTitle = new QLabel("⚙   Cài đặt", titleBar);
    tbTitle->setStyleSheet(QString("color: %1; font-size: 13pt; font-weight: 600;")
                               .arg(StyleTheme::ColorTextPrimary));
    auto* closeBtn = new QPushButton("✕", titleBar);
    closeBtn->setFixedSize(30, 30);
    closeBtn->setStyleSheet(QString(R"(
        QPushButton { background: transparent; color: %1; border: none; font-size: 12pt; border-radius: 6px; }
        QPushButton:hover { background: #c0392b; color: white; }
    )").arg(StyleTheme::ColorTextSecondary));
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    tbLay->addWidget(tbTitle);
    tbLay->addStretch();
    tbLay->addWidget(closeBtn);
    root->addWidget(titleBar);

    // ── Body ──────────────────────────────────────────────────────────────────
    auto* body = new QWidget(this);
    auto* bodyLay = new QHBoxLayout(body);
    bodyLay->setContentsMargins(0, 0, 0, 0);
    bodyLay->setSpacing(0);

    // ── Sidebar ───────────────────────────────────────────────────────────────
    m_sidebar = new QListWidget(body);
    m_sidebar->setFixedWidth(190);
    m_sidebar->setStyleSheet(QString(R"(
        QListWidget {
            background: %1;
            border: none;
            border-right: 1px solid %2;
            padding: 8px;
            outline: 0;
        }
        QListWidget::item {
            padding: 10px 14px;
            border-radius: 8px;
            color: %3;
            font-size: 10pt;
        }
        QListWidget::item:hover   { background: %4; color: %5; }
        QListWidget::item:selected { background: %6; color: %5; }
    )").arg(StyleTheme::ColorBackground)
        .arg(StyleTheme::ColorBorder)
        .arg(StyleTheme::ColorTextSecondary)
        .arg(StyleTheme::ColorSidebarHover)
        .arg(StyleTheme::ColorTextPrimary)
        .arg(StyleTheme::ColorSidebarActive));

    const QStringList sections = {
        "⚙  Chung",
        "🔤  Dịch thuật",
        "🔲  Nhận dạng chữ (OCR)",
        "🖥  Hiển thị (Overlay)",
        "📊  Hiệu suất",
        "⌨  Phím tắt",
        "🛠  Nâng cao"
    };
    for (const auto& s : sections) m_sidebar->addItem(s);

    // ── Detail stack ──────────────────────────────────────────────────────────
    m_stack = new QStackedWidget(body);
    m_stack->addWidget(buildGeneralPage());
    m_stack->addWidget(buildTranslationPage());
    m_stack->addWidget(buildOcrPage());
    m_stack->addWidget(buildOverlayPage());
    m_stack->addWidget(buildPerformancePage());
    m_stack->addWidget(buildHotkeysPage());
    m_stack->addWidget(buildAdvancedPage());

    connect(m_sidebar, &QListWidget::currentRowChanged, m_stack, &QStackedWidget::setCurrentIndex);
    m_sidebar->setCurrentRow(0);

    bodyLay->addWidget(m_sidebar);
    bodyLay->addWidget(m_stack, 1);
    root->addWidget(body, 1);

    // ── Footer buttons ────────────────────────────────────────────────────────
    auto* footer = new QWidget(this);
    footer->setFixedHeight(56);
    footer->setStyleSheet(QString("background: %1; border-top: 1px solid %2;")
                              .arg(StyleTheme::ColorSurface).arg(StyleTheme::ColorBorder));
    auto* footerLay = new QHBoxLayout(footer);
    footerLay->setContentsMargins(20, 0, 20, 0);
    footerLay->setSpacing(10);
    footerLay->addStretch();

    auto* cancelBtn = new SecondaryButton("Hủy", footer);
    cancelBtn->setFixedWidth(100);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    auto* saveBtn = new PrimaryButton("Lưu cài đặt", footer);
    saveBtn->setFixedWidth(130);
    connect(saveBtn, &QPushButton::clicked, this, [this]() {
        emit settingsSaved(m_settings);
        accept();
    });

    footerLay->addWidget(cancelBtn);
    footerLay->addWidget(saveBtn);
    root->addWidget(footer);
}

// ─── Pages ───────────────────────────────────────────────────────────────────
static QWidget* makePage()
{
    auto* p = new QWidget();
    p->setStyleSheet("background: transparent;");
    return p;
}

static QFormLayout* makeForm(QWidget* parent)
{
    auto* f = new QFormLayout();
    f->setContentsMargins(28, 24, 28, 24);
    f->setSpacing(16);
    f->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    return f;
}

static QLabel* makeFieldLabel(const QString& text)
{
    auto* l = new QLabel(text);
    l->setStyleSheet(QString("color: %1; font-size: 10pt;").arg(StyleTheme::ColorTextPrimary));
    return l;
}

QWidget* SettingsDialog::buildGeneralPage()
{
    auto* page = makePage();
    auto* lay  = new QVBoxLayout(page);
    lay->setContentsMargins(28, 24, 28, 24);
    lay->setSpacing(14);

    lay->addWidget(makePageTitle("⚙", "Chung"));
    lay->addWidget(makeDivider());
    lay->addSpacing(6);

    auto makeCheck = [](const QString& text) {
        auto* cb = new QCheckBox(text);
        cb->setStyleSheet(QString("color: %1; font-size: 10pt;").arg(StyleTheme::ColorTextPrimary));
        return cb;
    };

    lay->addWidget(makeCheck("Khởi động cùng Windows"));
    lay->addWidget(makeCheck("Thu nhỏ xuống khay hệ thống khi đóng"));
    auto* updateCheck = makeCheck("Kiểm tra cập nhật tự động");
    updateCheck->setChecked(true);
    lay->addWidget(updateCheck);

    lay->addSpacing(10);
    lay->addWidget(makeFieldLabel("Ngôn ngữ giao diện"));
    auto* langCombo = new QComboBox();
    langCombo->addItems({"Tiếng Việt", "English"});
    lay->addWidget(langCombo);

    lay->addStretch();
    return page;
}

QWidget* SettingsDialog::buildTranslationPage()
{
    auto* page = makePage();
    auto* lay  = new QVBoxLayout(page);
    lay->setContentsMargins(28, 24, 28, 24);
    lay->setSpacing(14);

    lay->addWidget(makePageTitle("🔤", "Dịch thuật"));
    lay->addWidget(makeDivider());
    lay->addSpacing(6);

    lay->addWidget(makeFieldLabel("Engine dịch thuật"));
    auto* engineCombo = new QComboBox();
    engineCombo->addItems({"🔌  Offline (Không cần mạng)", "🌐  Google Translate (Cần mạng)"});
    lay->addWidget(engineCombo);

    lay->addSpacing(8);
    lay->addWidget(makeFieldLabel("Số câu lưu trong bộ nhớ đệm"));
    auto* cacheSpin = new QSpinBox();
    cacheSpin->setRange(100, 20000);
    cacheSpin->setValue(5000);
    cacheSpin->setSuffix(" câu");
    lay->addWidget(cacheSpin);

    lay->addStretch();
    return page;
}

QWidget* SettingsDialog::buildOcrPage()
{
    auto* page = makePage();
    auto* lay  = new QVBoxLayout(page);
    lay->setContentsMargins(28, 24, 28, 24);
    lay->setSpacing(14);

    lay->addWidget(makePageTitle("🔲", "Nhận dạng chữ (OCR)"));
    lay->addWidget(makeDivider());
    lay->addSpacing(6);

    lay->addWidget(makeFieldLabel("Engine OCR"));
    auto* ocrCombo = new QComboBox();
    ocrCombo->addItems({"Windows Media OCR", "Tesseract OCR", "PaddleOCR (Placeholder)"});
    lay->addWidget(ocrCombo);

    lay->addStretch();
    return page;
}

QWidget* SettingsDialog::buildOverlayPage()
{
    auto* page = makePage();
    auto* lay  = new QVBoxLayout(page);
    lay->setContentsMargins(28, 24, 28, 24);
    lay->setSpacing(14);

    lay->addWidget(makePageTitle("🖥", "Hiển thị (Overlay)"));
    lay->addWidget(makeDivider());
    lay->addSpacing(6);

    lay->addWidget(makeFieldLabel("Độ trong suốt lớp phủ"));
    auto* opSlider = new QSlider(Qt::Horizontal);
    opSlider->setRange(10, 100);
    opSlider->setValue(100);
    lay->addWidget(opSlider);

    lay->addSpacing(8);
    lay->addWidget(makeFieldLabel("Font chữ mặc định"));
    auto* fontCombo = new QComboBox();
    fontCombo->addItems({"Segoe UI", "Arial", "Tahoma", "Times New Roman"});
    lay->addWidget(fontCombo);

    lay->addStretch();
    return page;
}

QWidget* SettingsDialog::buildPerformancePage()
{
    auto* page = makePage();
    auto* lay  = new QVBoxLayout(page);
    lay->setContentsMargins(28, 24, 28, 24);
    lay->setSpacing(14);

    lay->addWidget(makePageTitle("📊", "Hiệu suất"));
    lay->addWidget(makeDivider());
    lay->addSpacing(6);

    lay->addWidget(makeFieldLabel("Tần số quét khung hình (FPS)"));
    auto* fpsCombo = new QComboBox();
    fpsCombo->addItems({"15 FPS", "30 FPS", "60 FPS"});
    fpsCombo->setCurrentIndex(1);
    lay->addWidget(fpsCombo);

    auto makeCheck = [](const QString& t) {
        auto* c = new QCheckBox(t);
        c->setStyleSheet(QString("color: %1; font-size: 10pt;").arg(StyleTheme::ColorTextPrimary));
        return c;
    };

    auto* skipCheck = makeCheck("Bỏ qua khung hình tĩnh (Tiết kiệm CPU)");
    skipCheck->setChecked(true);
    lay->addWidget(skipCheck);
    lay->addWidget(makeCheck("Tận dụng GPU (Thử nghiệm)"));

    lay->addStretch();
    return page;
}

QWidget* SettingsDialog::buildHotkeysPage()
{
    auto* page = makePage();
    auto* lay  = new QVBoxLayout(page);
    lay->setContentsMargins(28, 24, 28, 24);
    lay->setSpacing(14);

    lay->addWidget(makePageTitle("⌨", "Phím tắt"));
    lay->addWidget(makeDivider());
    lay->addSpacing(6);

    struct HotkeyRow { QString label; QString defaultKey; };
    const QList<HotkeyRow> rows = {
        {"Bật / Tắt dịch",                "F8"},
        {"Ẩn / Hiện lớp phủ (Overlay)",   "F9"},
        {"Dịch nhanh vùng hiện tại",       "F10"},
    };

    for (const auto& row : rows) {
        lay->addWidget(makeFieldLabel(row.label));
        auto* keyEdit = new QComboBox();
        keyEdit->addItems({row.defaultKey, "(Nhấn để đổi phím)"});
        lay->addWidget(keyEdit);
        lay->addSpacing(4);
    }

    lay->addStretch();
    return page;
}

QWidget* SettingsDialog::buildAdvancedPage()
{
    auto* page = makePage();
    auto* lay  = new QVBoxLayout(page);
    lay->setContentsMargins(28, 24, 28, 24);
    lay->setSpacing(14);

    lay->addWidget(makePageTitle("🛠", "Nâng cao"));
    lay->addWidget(makeDivider());
    lay->addSpacing(6);

    auto makeBtn = [](const QString& text) {
        auto* btn = new QPushButton(text);
        btn->setFixedHeight(36);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(QString(R"(
            QPushButton {
                background: %1; color: %2;
                border: 1px solid %3; border-radius: 8px;
                font-size: 10pt; padding: 0 14px;
            }
            QPushButton:hover { background: %4; }
        )").arg(StyleTheme::ColorSurface)
            .arg(StyleTheme::ColorTextPrimary)
            .arg(StyleTheme::ColorBorder)
            .arg(StyleTheme::ColorSurfaceHigh));
        return btn;
    };

    lay->addWidget(makeBtn("📁  Mở thư mục dữ liệu"));
    lay->addWidget(makeBtn("📤  Xuất cấu hình (Export)"));
    lay->addWidget(makeBtn("📥  Nhập cấu hình (Import)"));
    lay->addWidget(makeBtn("🗑  Đặt lại về mặc định"));

    lay->addStretch();
    return page;
}
