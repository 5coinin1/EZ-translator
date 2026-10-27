#include "SettingsDialog.h"
#include "ui/theme/StyleTheme.h"
#include "ui/components/PrimaryButton.h"
#include "ui/components/SecondaryButton.h"
#include "ui/components/ToggleSwitch.h"
#include "platform/FramelessWindowHelper.h"

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
#include <QColorDialog>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPainter>
#include <QScrollArea>
#include <QMouseEvent>
#include <functional>
#include <algorithm>

namespace {

class ColorCircleButton : public QPushButton
{
public:
    explicit ColorCircleButton(const QColor& initialColor, QWidget* parent = nullptr)
        : QPushButton(parent), m_color(initialColor)
    {
        setFixedSize(36, 36);
        setCursor(Qt::PointingHandCursor);
        setStyleSheet("QPushButton { border: none; background: transparent; }");
    }

    QColor color() const { return m_color; }
    void setColor(const QColor& c) {
        m_color = c;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        QRect r = rect().adjusted(3, 3, -3, -3);
        p.setPen(QPen(QColor("#64748b"), 2.0f));
        p.setBrush(m_color);
        p.drawEllipse(r);

        if (m_color.lightness() > 200) {
            p.setPen(QPen(QColor(0, 0, 0, 50), 1.0f));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(r.adjusted(1, 1, -1, -1));
        }
    }

private:
    QColor m_color;
};

class CompactStepper : public QWidget
{
public:
    explicit CompactStepper(int minVal, int maxVal, int initialVal, QWidget* parent = nullptr)
        : QWidget(parent), m_min(minVal), m_max(maxVal), m_val(initialVal)
    {
        setFixedWidth(34);
        auto* lay = new QVBoxLayout(this);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(0);

        m_upBtn = new QPushButton("▲", this);
        m_upBtn->setFixedSize(30, 14);
        m_upBtn->setStyleSheet(R"(
            QPushButton { background: transparent; color: #94a3b8; border: none; font-size: 7.5pt; font-weight: bold; }
            QPushButton:hover { color: #38bdf8; }
            QPushButton:pressed { color: #0284c7; }
        )");

        m_label = new QLabel(QString::number(m_val), this);
        m_label->setAlignment(Qt::AlignCenter);
        m_label->setStyleSheet("color: #f8fafc; font-size: 11.5pt; font-weight: bold; background: transparent;");

        m_downBtn = new QPushButton("▼", this);
        m_downBtn->setFixedSize(30, 14);
        m_downBtn->setStyleSheet(R"(
            QPushButton { background: transparent; color: #94a3b8; border: none; font-size: 7.5pt; font-weight: bold; }
            QPushButton:hover { color: #38bdf8; }
            QPushButton:pressed { color: #0284c7; }
        )");

        lay->addWidget(m_upBtn, 0, Qt::AlignCenter);
        lay->addWidget(m_label, 0, Qt::AlignCenter);
        lay->addWidget(m_downBtn, 0, Qt::AlignCenter);

        connect(m_upBtn, &QPushButton::clicked, this, [this]() {
            if (m_val < m_max) {
                m_val++;
                m_label->setText(QString::number(m_val));
                if (m_onChanged) m_onChanged(m_val);
            }
        });

        connect(m_downBtn, &QPushButton::clicked, this, [this]() {
            if (m_val > m_min) {
                m_val--;
                m_label->setText(QString::number(m_val));
                if (m_onChanged) m_onChanged(m_val);
            }
        });
    }

    int value() const { return m_val; }
    void setValue(int v) {
        m_val = std::clamp(v, m_min, m_max);
        m_label->setText(QString::number(m_val));
        if (m_onChanged) m_onChanged(m_val);
    }

    void setOnChanged(std::function<void(int)> cb) { m_onChanged = std::move(cb); }

private:
    int m_min;
    int m_max;
    int m_val;
    QPushButton* m_upBtn{nullptr};
    QPushButton* m_downBtn{nullptr};
    QLabel* m_label{nullptr};
    std::function<void(int)> m_onChanged;
};

/**
 * Thanh tiêu đề của dialog frameless: kéo được như thanh tiêu đề Windows.
 *
 * Dialog không dùng TitleBar chung vì bố cục khác (chỉ có tiêu đề + nút đóng).
 */
class DialogTitleBar : public QWidget
{
public:
    explicit DialogTitleBar(QWidget* parent = nullptr) : QWidget(parent) {}

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton) {
            QWidget::mousePressEvent(event);
            return;
        }

        m_dragging = true;
        m_dragStartPos = event->globalPosition().toPoint() - window()->frameGeometry().topLeft();
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (m_dragging && (event->buttons() & Qt::LeftButton)) {
            window()->move(event->globalPosition().toPoint() - m_dragStartPos);
            event->accept();
            return;
        }
        QWidget::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        m_dragging = false;
        QWidget::mouseReleaseEvent(event);
    }

private:
    bool   m_dragging{false};
    QPoint m_dragStartPos;
};

} // namespace

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

bool SettingsDialog::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
    // Frameless dialog: bật resize bằng viền (kéo do DialogTitleBar đảm nhiệm).
    if (EZTranslator::FramelessWindow::handleNativeMessage(this, eventType, message, result))
        return true;
    return QDialog::nativeEvent(eventType, message, result);
}

// ─── Helper to create a page heading ─────────────────────────────────────────
static QLabel* makePageTitle(const QString& title)
{
    auto* l = new QLabel(title);
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
    auto* titleBar = new DialogTitleBar(this);
    titleBar->setFixedHeight(50);
    titleBar->setStyleSheet(QString("background: %1; border-bottom: 1px solid %2;")
                                .arg(StyleTheme::ColorBackground).arg(StyleTheme::ColorBorder));
    auto* tbLay = new QHBoxLayout(titleBar);
    tbLay->setContentsMargins(20, 0, 16, 0);
    auto* tbTitle = new QLabel("Cài đặt", titleBar);
    // Nhường chuột cho thanh tiêu đề để kéo cửa sổ được từ cả vùng chữ.
    tbTitle->setAttribute(Qt::WA_TransparentForMouseEvents);
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
    m_sidebar->setFixedWidth(180);
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
        "Chung",
        "Dịch thuật",
        "Giao diện",
        "OCR",
        "Phím tắt"
    };
    for (const auto& s : sections) m_sidebar->addItem(s);

    // ── Detail stack ──────────────────────────────────────────────────────────
    m_stack = new QStackedWidget(body);
    m_stack->addWidget(buildGeneralPage());
    m_stack->addWidget(buildTranslationPage());
    m_stack->addWidget(buildOverlayPage());
    m_stack->addWidget(buildOcrPage());
    m_stack->addWidget(buildHotkeysPage());

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
    lay->setSpacing(16);

    lay->addWidget(makePageTitle("Chung"));
    lay->addWidget(makeDivider());
    lay->addSpacing(6);

    auto makeItemLabel = [](const QString& text) {
        auto* l = new QLabel(text);
        l->setStyleSheet("color: #e2e8f0; font-size: 10.5pt; font-weight: 500;");
        return l;
    };

    // ── Row 1: [ None ▼ ] Text to speech system ───────────────────────────────
    auto* ttsRow = new QHBoxLayout();
    ttsRow->setSpacing(16);
    auto* ttsCombo = new QComboBox(page);
    ttsCombo->setFixedWidth(180);
    ttsCombo->setFixedHeight(36);
    ttsCombo->addItems({"None", "Windows TTS", "Google TTS"});
    ttsCombo->setStyleSheet(R"(
        QComboBox {
            background-color: #0b1320;
            border: 1px solid #334155;
            border-radius: 6px;
            color: #f8fafc;
            padding: 4px 12px;
            font-size: 10pt;
        }
        QComboBox:hover { border-color: #38bdf8; }
        QComboBox::drop-down { border: none; width: 24px; }
        QComboBox QAbstractItemView {
            background-color: #0f172a;
            color: #f8fafc;
            selection-background-color: #1e3a8a;
            border: 1px solid #334155;
            border-radius: 6px;
            padding: 4px;
        }
    )");
    ttsRow->addWidget(ttsCombo);
    ttsRow->addWidget(makeItemLabel("Text to speech system"));
    ttsRow->addStretch();
    lay->addLayout(ttsRow);

    lay->addSpacing(8);
    lay->addWidget(makeFieldLabel("Ngôn ngữ giao diện"));
    auto* langCombo = new QComboBox(page);
    langCombo->setFixedWidth(200);
    langCombo->addItems({"Tiếng Việt", "English"});
    langCombo->setStyleSheet(R"(
        QComboBox {
            background-color: #0b1320;
            border: 1px solid #334155;
            border-radius: 6px;
            color: #f8fafc;
            padding: 4px 12px;
            font-size: 10pt;
        }
        QComboBox:hover { border-color: #38bdf8; }
        QComboBox::drop-down { border: none; width: 24px; }
    )");
    lay->addWidget(langCombo);

    lay->addStretch();
    return page;
}

QWidget* SettingsDialog::buildTranslationPage()
{
    auto* page = makePage();
    auto* mainLay = new QVBoxLayout(page);
    mainLay->setContentsMargins(28, 24, 28, 20);
    mainLay->setSpacing(12);

    mainLay->addWidget(makePageTitle("Dịch thuật"));
    mainLay->addWidget(makeDivider());
    mainLay->addSpacing(4);

    auto* scroll = new QScrollArea(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet("QScrollArea { background: transparent; border: none; }");

    auto* content = new QWidget();
    content->setStyleSheet("background: transparent;");
    auto* lay = new QVBoxLayout(content);
    lay->setContentsMargins(0, 4, 10, 0);
    lay->setSpacing(10);

    auto makeItemLabel = [](const QString& text) {
        auto* l = new QLabel(text);
        l->setStyleSheet("color: #e2e8f0; font-size: 10.5pt; font-weight: 500;");
        return l;
    };

    auto makeSectionLabel = [](const QString& text) {
        auto* l = new QLabel(text);
        l->setStyleSheet("color: #38bdf8; font-size: 10pt; font-weight: 700;");
        return l;
    };

    auto makeHint = [](const QString& text) {
        auto* l = new QLabel(text);
        l->setWordWrap(true);
        l->setStyleSheet("color: #94a3b8; font-size: 8.5pt; background: transparent;");
        return l;
    };

    const QString comboStyle = R"(
        QComboBox {
            background-color: #0b1320;
            border: 1px solid #334155;
            border-radius: 6px;
            color: #f8fafc;
            padding: 4px 12px;
            font-size: 10pt;
        }
        QComboBox:hover { border-color: #38bdf8; }
        QComboBox::drop-down { border: none; width: 24px; }
        QComboBox QAbstractItemView {
            background-color: #0f172a;
            color: #f8fafc;
            selection-background-color: #1e3a8a;
            border: 1px solid #334155;
            border-radius: 6px;
            padding: 4px;
        }
    )";

    const QString inputStyle = R"(
        QLineEdit, QPlainTextEdit {
            background-color: #0b1320;
            border: 1px solid #334155;
            border-radius: 6px;
            color: #f8fafc;
            padding: 7px 10px;
            font-size: 10pt;
            selection-background-color: #1e3a8a;
        }
        QLineEdit:hover, QPlainTextEdit:hover { border-color: #38bdf8; }
        QLineEdit:focus, QPlainTextEdit:focus { border-color: #38bdf8; }
    )";

    // ── Engine ────────────────────────────────────────────────────────────────
    lay->addWidget(makeSectionLabel("Engine dịch"));
    m_translationEngine = new QComboBox(content);
    m_translationEngine->setFixedHeight(36);
    m_translationEngine->setStyleSheet(comboStyle);
    m_translationEngine->addItem("Tự động (Offline → Google)", "auto");
    m_translationEngine->addItem("Google Translate", "google");
    m_translationEngine->addItem("AI / LLM (OpenAI-compatible)", "ai");
    m_translationEngine->addItem("Offline (trên máy)", "offline");
    lay->addWidget(m_translationEngine);
    lay->addWidget(makeHint(
        "Tự động: dùng model offline nếu có, không thì Google. "
        "Offline: chỉ dịch trên máy (không gửi dữ liệu ra mạng)."));

    // ── Google ────────────────────────────────────────────────────────────────
    lay->addSpacing(10);
    lay->addWidget(makeSectionLabel("Google Translate"));
    lay->addWidget(makeItemLabel("API key (tùy chọn)"));
    m_googleApiKey = new QLineEdit(content);
    m_googleApiKey->setFixedHeight(36);
    m_googleApiKey->setStyleSheet(inputStyle);
    m_googleApiKey->setPlaceholderText("Để trống để dùng endpoint miễn phí");
    lay->addWidget(m_googleApiKey);
    lay->addWidget(makeHint(
        "Có key → dùng Cloud Translation API chính thức (cần bật billing). "
        "Để trống → dùng translate.googleapis.com miễn phí, có thể bị giới hạn."));

    // ── AI / LLM ──────────────────────────────────────────────────────────────
    lay->addSpacing(10);
    m_aiGroup = new QWidget(content);
    auto* aiLay = new QVBoxLayout(m_aiGroup);
    aiLay->setContentsMargins(0, 0, 0, 0);
    aiLay->setSpacing(8);

    aiLay->addWidget(makeSectionLabel("AI / LLM (OpenAI-compatible)"));
    aiLay->addWidget(makeItemLabel("Endpoint"));
    m_aiEndpoint = new QLineEdit(m_aiGroup);
    m_aiEndpoint->setFixedHeight(36);
    m_aiEndpoint->setStyleSheet(inputStyle);
    m_aiEndpoint->setPlaceholderText("https://api.openai.com/v1/chat/completions");
    aiLay->addWidget(m_aiEndpoint);
    aiLay->addWidget(makeHint(
        "Dùng được với OpenAI, OpenRouter, DeepSeek, Groq, Ollama, LM Studio… "
        "Endpoint phải là URL đầy đủ tới /chat/completions."));

    auto* keyModelRow = new QHBoxLayout();
    keyModelRow->setSpacing(12);

    auto* keyCol = new QVBoxLayout();
    keyCol->setSpacing(6);
    keyCol->addWidget(makeItemLabel("API key"));
    m_aiApiKey = new QLineEdit(m_aiGroup);
    m_aiApiKey->setFixedHeight(36);
    m_aiApiKey->setStyleSheet(inputStyle);
    m_aiApiKey->setEchoMode(QLineEdit::Password);
    m_aiApiKey->setPlaceholderText("sk-... (để trống nếu endpoint local)");
    keyCol->addWidget(m_aiApiKey);

    auto* modelCol = new QVBoxLayout();
    modelCol->setSpacing(6);
    modelCol->addWidget(makeItemLabel("Model"));
    m_aiModel = new QLineEdit(m_aiGroup);
    m_aiModel->setFixedHeight(36);
    m_aiModel->setStyleSheet(inputStyle);
    m_aiModel->setPlaceholderText("gpt-4o-mini");
    modelCol->addWidget(m_aiModel);

    keyModelRow->addLayout(keyCol, 1);
    keyModelRow->addLayout(modelCol, 1);
    aiLay->addLayout(keyModelRow);

    aiLay->addWidget(makeItemLabel("System prompt"));
    m_aiPrompt = new QPlainTextEdit(m_aiGroup);
    m_aiPrompt->setFixedHeight(130);
    m_aiPrompt->setStyleSheet(inputStyle);
    m_aiPrompt->setPlaceholderText(
        "Mô tả ngữ cảnh và cách dịch (thể loại game/truyện, cách xưng hô, "
        "giữ tên riêng…). Prompt này được gửi kèm mọi yêu cầu dịch.");
    aiLay->addWidget(m_aiPrompt);
    aiLay->addWidget(makeHint(
        "Prompt hệ thống giúp AI hiểu ngữ cảnh. Quy tắc trả về JSON được app tự "
        "thêm để ghép bản dịch đúng từng đoạn."));

    lay->addWidget(m_aiGroup);
    lay->addStretch();

    scroll->setWidget(content);
    mainLay->addWidget(scroll, 1);

    // ── Wiring ────────────────────────────────────────────────────────────────
    connect(m_translationEngine, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int index) {
                if (!m_translationEngine || index < 0)
                    return;
                m_settings.translationEngine = m_translationEngine->itemData(index).toString();
            });

    connect(m_googleApiKey, &QLineEdit::textChanged, this,
            [this](const QString& text) { m_settings.googleApiKey = text; });
    connect(m_aiEndpoint, &QLineEdit::textChanged, this,
            [this](const QString& text) { m_settings.aiEndpoint = text; });
    connect(m_aiApiKey, &QLineEdit::textChanged, this,
            [this](const QString& text) { m_settings.aiApiKey = text; });
    connect(m_aiModel, &QLineEdit::textChanged, this,
            [this](const QString& text) { m_settings.aiModel = text; });
    connect(m_aiPrompt, &QPlainTextEdit::textChanged, this, [this]() {
        if (m_aiPrompt)
            m_settings.aiSystemPrompt = m_aiPrompt->toPlainText();
    });

    return page;
}

void SettingsDialog::setSettings(const EZTranslator::AppSettings& settings)
{
    m_settings = settings;

    if (m_novelModeSwitch)
        m_novelModeSwitch->setChecked(m_settings.novelMode);
    if (m_gameDialogModeSwitch)
        m_gameDialogModeSwitch->setChecked(m_settings.gameDialogMode);
    if (m_fontFamilyCombo) {
        const int index = m_fontFamilyCombo->findText(m_settings.fontFamily);
        if (index >= 0)
            m_fontFamilyCombo->setCurrentIndex(index);
    }
    if (m_textWindowStyleCombo) {
        const int index = m_textWindowStyleCombo->findData(m_settings.textWindowStyle);
        if (index >= 0)
            m_textWindowStyleCombo->setCurrentIndex(index);
    }
    if (m_showSourceSwitch)
        m_showSourceSwitch->setChecked(m_settings.textWindowShowSource);

    if (auto* widget = findChild<QWidget*>(QStringLiteral("winColorBtn"))) {
        if (auto* button = dynamic_cast<ColorCircleButton*>(widget))
            button->setColor(m_settings.windowColor);
    }
    if (auto* widget = findChild<QWidget*>(QStringLiteral("fontColorBtn"))) {
        if (auto* button = dynamic_cast<ColorCircleButton*>(widget))
            button->setColor(m_settings.fontColor);
    }
    if (auto* widget = findChild<QWidget*>(QStringLiteral("fontSizeStepper"))) {
        if (auto* stepper = dynamic_cast<CompactStepper*>(widget))
            stepper->setValue(m_settings.fontSize);
    }
    if (auto* toggle = findChild<ToggleSwitch*>(QStringLiteral("boldSwitch")))
        toggle->setChecked(m_settings.isBold);
    if (auto* widget = findChild<QWidget*>(QStringLiteral("lineSpacingStepper"))) {
        if (auto* stepper = dynamic_cast<CompactStepper*>(widget))
            stepper->setValue(m_settings.lineSpacing);
    }
    if (auto* combo = findChild<QComboBox*>(QStringLiteral("alignCombo")))
        combo->setCurrentIndex(qBound(0, m_settings.textAlignment, 2));
    if (auto* slider = findChild<QSlider*>(QStringLiteral("opacitySlider")))
        slider->setValue(m_settings.windowOpacity);
    if (m_ocrGpu)
        m_ocrGpu->setChecked(m_settings.ocrUseGpu);
    if (m_ocrBinarize)
        m_ocrBinarize->setChecked(m_settings.ocrBinarize);
    updateOcrInfo();

    if (m_translationEngine) {
        const int index = m_translationEngine->findData(m_settings.translationEngine);
        m_translationEngine->setCurrentIndex(index >= 0 ? index : 0);
        if (index < 0)
            m_settings.translationEngine = QStringLiteral("auto");
    }
    if (m_googleApiKey)
        m_googleApiKey->setText(m_settings.googleApiKey);
    if (m_aiEndpoint)
        m_aiEndpoint->setText(m_settings.aiEndpoint);
    if (m_aiApiKey)
        m_aiApiKey->setText(m_settings.aiApiKey);
    if (m_aiModel)
        m_aiModel->setText(m_settings.aiModel);
    if (m_aiPrompt)
        m_aiPrompt->setPlainText(m_settings.aiSystemPrompt);

    updatePreview();
}

void SettingsDialog::updateOcrInfo()
{
    if (!m_ocrModelLabel)
        return;

    const QString source = m_settings.sourceLanguage;
    QString recognizer = QStringLiteral("ch_PP-OCRv4_rec_infer.onnx");
    QString language = QStringLiteral("Trung / mặc định");
    if (source.startsWith(QStringLiteral("en"), Qt::CaseInsensitive)) {
        recognizer = QStringLiteral("en_PP-OCRv4_rec_mobile.onnx");
        language = QStringLiteral("Anh");
    } else if (source.startsWith(QStringLiteral("ja"), Qt::CaseInsensitive)) {
        recognizer = QStringLiteral("japan_PP-OCRv4_rec_infer.onnx (fallback ch)");
        language = QStringLiteral("Nhật");
    }

    m_ocrModelLabel->setText(
        QStringLiteral("Ngôn ngữ nhận dạng: %1\n"
                       "Detector: ch_PP-OCRv4_det_infer.onnx\n"
                       "Recognizer: %2")
            .arg(language, recognizer));
}

void SettingsDialog::updatePreview()
{
    if (!m_previewFrame || !m_previewTextLabel) return;

    // Window color with opacity
    QColor bg = m_settings.windowColor;
    int alpha = qRound(m_settings.windowOpacity * 255.0 / 100.0);
    bg.setAlpha(alpha);

    // Styling frame
    m_previewFrame->setStyleSheet(QString(R"(
        QFrame#previewFrame {
            background-color: rgba(%1, %2, %3, %4);
            border: 1.5px solid #334155;
            border-radius: 8px;
        }
    )").arg(bg.red()).arg(bg.green()).arg(bg.blue()).arg(alpha / 255.0));

    // Text styling
    Qt::Alignment align = Qt::AlignLeft;
    if (m_settings.textAlignment == 1) align = Qt::AlignCenter;
    else if (m_settings.textAlignment == 2) align = Qt::AlignRight;
    m_previewTextLabel->setAlignment(align | Qt::AlignVCenter);

    QFont font("Segoe UI", m_settings.fontSize);
    font.setBold(m_settings.isBold);
    m_previewTextLabel->setFont(font);

    m_previewTextLabel->setStyleSheet(QString(R"(
        color: %1;
        background: transparent;
        border: none;
        line-height: %2px;
    )").arg(m_settings.fontColor.name()).arg(m_settings.lineSpacing));
}

QWidget* SettingsDialog::buildOverlayPage()
{
    auto* page = makePage();
    auto* mainLay = new QVBoxLayout(page);
    mainLay->setContentsMargins(28, 20, 28, 20);
    mainLay->setSpacing(12);

    mainLay->addWidget(makePageTitle("Giao diện"));
    mainLay->addWidget(makeDivider());
    mainLay->addSpacing(4);

    // Scrollable area for content
    auto* scroll = new QScrollArea(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet("QScrollArea { background: transparent; border: none; }");

    auto* scrollContent = new QWidget();
    scrollContent->setStyleSheet("background: transparent;");
    auto* lay = new QVBoxLayout(scrollContent);
    lay->setContentsMargins(0, 0, 10, 0);
    lay->setSpacing(16);

    auto makeItemLabel = [](const QString& text) {
        auto* l = new QLabel(text);
        l->setStyleSheet("color: #e2e8f0; font-size: 10.5pt; font-weight: 500;");
        return l;
    };

    // ── Row 1: Window color & Font color ──────────────────────────────────────
    auto* row1 = new QHBoxLayout();
    row1->setSpacing(24);

    auto* winColorRow = new QHBoxLayout();
    winColorRow->setSpacing(10);
    auto* winColorBtn = new ColorCircleButton(m_settings.windowColor, scrollContent);
    winColorBtn->setObjectName(QStringLiteral("winColorBtn"));
    winColorRow->addWidget(winColorBtn);
    winColorRow->addWidget(makeItemLabel("Window color"));
    winColorRow->addStretch();

    auto* fontColorRow = new QHBoxLayout();
    fontColorRow->setSpacing(10);
    auto* fontColorBtn = new ColorCircleButton(m_settings.fontColor, scrollContent);
    fontColorBtn->setObjectName(QStringLiteral("fontColorBtn"));
    fontColorRow->addWidget(fontColorBtn);
    fontColorRow->addWidget(makeItemLabel("Font color"));
    fontColorRow->addStretch();

    row1->addLayout(winColorRow, 1);
    row1->addLayout(fontColorRow, 1);
    lay->addLayout(row1);

    // ── Row 2: Font size & Bold ───────────────────────────────────────────────
    auto* row2 = new QHBoxLayout();
    row2->setSpacing(24);

    auto* fontSizeRow = new QHBoxLayout();
    fontSizeRow->setSpacing(12);
    auto* fontSizeStepper = new CompactStepper(8, 72, m_settings.fontSize, scrollContent);
    fontSizeStepper->setObjectName(QStringLiteral("fontSizeStepper"));
    fontSizeRow->addWidget(fontSizeStepper);
    fontSizeRow->addWidget(makeItemLabel("Font size"));
    fontSizeRow->addStretch();

    auto* boldRow = new QHBoxLayout();
    boldRow->setSpacing(12);
    auto* boldSwitch = new ToggleSwitch(scrollContent);
    boldSwitch->setObjectName(QStringLiteral("boldSwitch"));
    boldSwitch->setChecked(m_settings.isBold);
    boldRow->addWidget(boldSwitch);
    boldRow->addWidget(makeItemLabel("Bold"));
    boldRow->addStretch();

    row2->addLayout(fontSizeRow, 1);
    row2->addLayout(boldRow, 1);
    lay->addLayout(row2);

    // Font + kiểu hiển thị dùng cho popup dịch khi chỉ chuột và cửa sổ nổi.
    auto* rowTextStyle = new QHBoxLayout();
    rowTextStyle->setSpacing(12);

    m_fontFamilyCombo = new QComboBox(scrollContent);
    m_fontFamilyCombo->addItems({"Segoe UI", "Arial", "Tahoma", "Times New Roman", "Consolas"});
    m_fontFamilyCombo->setCurrentText(m_settings.fontFamily);
    m_fontFamilyCombo->setFixedWidth(180);

    m_textWindowStyleCombo = new QComboBox(scrollContent);
    m_textWindowStyleCombo->addItem("Solid", 0);
    m_textWindowStyleCombo->addItem("Glass", 1);
    m_textWindowStyleCombo->addItem("Outline", 2);
    m_textWindowStyleCombo->addItem("Paper", 3);
    m_textWindowStyleCombo->setCurrentIndex(
        qBound(0, m_textWindowStyleCombo->findData(m_settings.textWindowStyle), 3));
    m_textWindowStyleCombo->setFixedWidth(120);

    m_showSourceSwitch = new ToggleSwitch(scrollContent);
    m_showSourceSwitch->setChecked(m_settings.textWindowShowSource);

    rowTextStyle->addWidget(m_fontFamilyCombo, 1);
    rowTextStyle->addWidget(makeItemLabel("Font"));
    rowTextStyle->addWidget(m_textWindowStyleCombo, 1);
    rowTextStyle->addWidget(makeItemLabel("Style"));
    rowTextStyle->addSpacing(16);
    rowTextStyle->addWidget(m_showSourceSwitch);
    rowTextStyle->addWidget(makeItemLabel("Show source"));
    rowTextStyle->addStretch();
    lay->addLayout(rowTextStyle);

    // ── Row 3: Line spacing & Keep source formatting ──────────────────────────
    auto* row3 = new QHBoxLayout();
    row3->setSpacing(24);

    auto* lineSpacingRow = new QHBoxLayout();
    lineSpacingRow->setSpacing(12);
    auto* lineSpacingStepper = new CompactStepper(6, 60, m_settings.lineSpacing, scrollContent);
    lineSpacingStepper->setObjectName(QStringLiteral("lineSpacingStepper"));
    lineSpacingRow->addWidget(lineSpacingStepper);
    lineSpacingRow->addWidget(makeItemLabel("Line spacing"));
    lineSpacingRow->addStretch();

    auto* keepFormatRow = new QHBoxLayout();
    keepFormatRow->setSpacing(12);
    auto* keepFormatSwitch = new ToggleSwitch(scrollContent);
    keepFormatSwitch->setChecked(m_settings.keepSourceFormatting);
    keepFormatRow->addWidget(keepFormatSwitch);
    keepFormatRow->addWidget(makeItemLabel("Keep source formatting"));
    keepFormatRow->addStretch();

    row3->addLayout(lineSpacingRow, 1);
    row3->addLayout(keepFormatRow, 1);
    lay->addLayout(row3);

    // ── Row 4: Text alignment combobox ────────────────────────────────────────
    auto* alignCombo = new QComboBox(scrollContent);
    alignCombo->setObjectName(QStringLiteral("alignCombo"));
    alignCombo->setFixedWidth(240);
    alignCombo->setFixedHeight(36);
    alignCombo->addItems({"Align Text Left", "Align Text Center", "Align Text Right"});
    alignCombo->setCurrentIndex(m_settings.textAlignment);
    alignCombo->setStyleSheet(R"(
        QComboBox {
            background-color: #0b1320;
            border: 1px solid #334155;
            border-radius: 6px;
            color: #f8fafc;
            padding: 4px 12px;
            font-size: 10pt;
        }
        QComboBox:hover {
            border-color: #38bdf8;
        }
        QComboBox::drop-down {
            border: none;
            width: 24px;
        }
        QComboBox QAbstractItemView {
            background-color: #0f172a;
            color: #f8fafc;
            selection-background-color: #1e3a8a;
            border: 1px solid #334155;
            border-radius: 6px;
            padding: 4px;
        }
    )");
    lay->addWidget(alignCombo);

    // ── Row 5: Auto clear window ──────────────────────────────────────────────
    auto* autoClearRow = new QHBoxLayout();
    autoClearRow->setSpacing(12);
    auto* autoClearSwitch = new ToggleSwitch(scrollContent);
    autoClearSwitch->setChecked(m_settings.autoClearWindow);
    autoClearRow->addWidget(autoClearSwitch);
    autoClearRow->addWidget(makeItemLabel("Auto clear window"));
    autoClearRow->addStretch();
    lay->addLayout(autoClearRow);

    // Novel mode: ép layout đọc truyện, không tự chuyển sang UI box/glass.
    auto* novelModeRow = new QHBoxLayout();
    novelModeRow->setSpacing(12);
    m_novelModeSwitch = new ToggleSwitch(scrollContent);
    m_novelModeSwitch->setChecked(m_settings.novelMode);
    m_novelModeSwitch->setToolTip(
        "Luôn dùng layout đọc truyện (đoạn văn + nền giấy), không tự chuyển sang UI.");
    novelModeRow->addWidget(m_novelModeSwitch);
    novelModeRow->addWidget(makeItemLabel("Novel mode (paper layout)"));
    novelModeRow->addStretch();
    lay->addLayout(novelModeRow);

    connect(m_novelModeSwitch, &QAbstractButton::toggled, this, [this](bool enabled) {
        m_settings.novelMode = enabled;
    });

    auto* gameDialogRow = new QHBoxLayout();
    gameDialogRow->setSpacing(12);
    m_gameDialogModeSwitch = new ToggleSwitch(scrollContent);
    m_gameDialogModeSwitch->setObjectName(QStringLiteral("gameDialogModeSwitch"));
    m_gameDialogModeSwitch->setChecked(m_settings.gameDialogMode);
    m_gameDialogModeSwitch->setToolTip(
        "So sánh vùng chữ đã nhận diện để bỏ qua nền game chuyển động. Hãy chọn ROI gọn quanh hộp thoại.");
    gameDialogRow->addWidget(m_gameDialogModeSwitch);
    gameDialogRow->addWidget(makeItemLabel("Game dialogue mode"));
    gameDialogRow->addStretch();
    lay->addLayout(gameDialogRow);
    connect(m_gameDialogModeSwitch, &QAbstractButton::toggled, this, [this](bool enabled) {
        m_settings.gameDialogMode = enabled;
    });

    // ── Row 6: Exclude from capture ───────────────────────────────────────────
    auto* excludeCaptureRow = new QHBoxLayout();
    excludeCaptureRow->setSpacing(12);
    auto* excludeCaptureSwitch = new ToggleSwitch(scrollContent);
    excludeCaptureSwitch->setChecked(m_settings.excludeFromCapture);
    excludeCaptureRow->addWidget(excludeCaptureSwitch);
    excludeCaptureRow->addWidget(makeItemLabel("Exclude from capture"));
    excludeCaptureRow->addStretch();
    lay->addLayout(excludeCaptureRow);

    // ── Row 7: Window opacity slider ──────────────────────────────────────────
    auto* opacityLay = new QVBoxLayout();
    opacityLay->setSpacing(6);

    auto* opLabelRow = new QHBoxLayout();
    opLabelRow->addWidget(makeItemLabel("Window opacity"));
    auto* opValLabel = new QLabel(QString("%1%").arg(m_settings.windowOpacity), scrollContent);
    opValLabel->setStyleSheet("color: #38bdf8; font-size: 10pt; font-weight: bold;");
    opLabelRow->addWidget(opValLabel);
    opLabelRow->addStretch();
    opacityLay->addLayout(opLabelRow);

    auto* opacitySlider = new QSlider(Qt::Horizontal, scrollContent);
    opacitySlider->setObjectName(QStringLiteral("opacitySlider"));
    opacitySlider->setRange(10, 100);
    opacitySlider->setValue(m_settings.windowOpacity);
    opacitySlider->setStyleSheet(R"(
        QSlider::groove:horizontal {
            height: 6px;
            background: #1e293b;
            border-radius: 3px;
        }
        QSlider::sub-page:horizontal {
            background: #38bdf8;
            border-radius: 3px;
        }
        QSlider::handle:horizontal {
            background: #e0f2fe;
            border: 2px solid #0284c7;
            width: 18px;
            margin-top: -6px;
            margin-bottom: -6px;
            border-radius: 9px;
        }
        QSlider::handle:horizontal:hover {
            background: #ffffff;
            border-color: #38bdf8;
        }
    )");
    opacityLay->addWidget(opacitySlider);
    lay->addLayout(opacityLay);

    // ── Row 8: Show text example button & Preview Card ────────────────────────
    auto* previewBtnRow = new QHBoxLayout();
    auto* previewBtn = new QPushButton("Show text example", scrollContent);
    previewBtn->setFixedHeight(36);
    previewBtn->setFixedWidth(170);
    previewBtn->setCursor(Qt::PointingHandCursor);
    previewBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #bae6fd;
            color: #0369a1;
            border: none;
            border-radius: 6px;
            font-size: 9.5pt;
            font-weight: bold;
            padding: 0 16px;
        }
        QPushButton:hover {
            background-color: #7dd3fc;
            color: #075985;
        }
        QPushButton:pressed {
            background-color: #38bdf8;
            color: #0c4a6e;
        }
    )");
    previewBtnRow->addWidget(previewBtn);
    previewBtnRow->addStretch();
    lay->addLayout(previewBtnRow);

    // Live preview frame
    m_previewFrame = new QFrame(scrollContent);
    m_previewFrame->setObjectName("previewFrame");
    m_previewFrame->setMinimumHeight(80);

    auto* pfLay = new QVBoxLayout(m_previewFrame);
    pfLay->setContentsMargins(16, 12, 16, 12);
    pfLay->setSpacing(6);

    auto* sampleTag = new QLabel("VÍ DỤ HIỂN THỊ VĂN BẢN DỊCH", m_previewFrame);
    sampleTag->setStyleSheet("color: #64748b; font-size: 7.5pt; font-weight: bold; letter-spacing: 1px;");
    pfLay->addWidget(sampleTag);

    m_previewTextLabel = new QLabel("Chào mừng bạn đến với EZ-Translator!\nWelcome to EZ-Translator real-time game subtitle.", m_previewFrame);
    m_previewTextLabel->setWordWrap(true);
    pfLay->addWidget(m_previewTextLabel);

    lay->addWidget(m_previewFrame);
    lay->addStretch();

    scroll->setWidget(scrollContent);
    mainLay->addWidget(scroll, 1);

    // Connect events to update settings and live preview
    connect(winColorBtn, &QPushButton::clicked, this, [this, winColorBtn]() {
        QColor chosen = QColorDialog::getColor(m_settings.windowColor, this, "Chọn màu nền cửa sổ", QColorDialog::ShowAlphaChannel);
        if (chosen.isValid()) {
            m_settings.windowColor = chosen;
            winColorBtn->setColor(chosen);
            updatePreview();
        }
    });

    connect(fontColorBtn, &QPushButton::clicked, this, [this, fontColorBtn]() {
        QColor chosen = QColorDialog::getColor(m_settings.fontColor, this, "Chọn màu chữ dịch");
        if (chosen.isValid()) {
            m_settings.fontColor = chosen;
            fontColorBtn->setColor(chosen);
            updatePreview();
        }
    });

    fontSizeStepper->setOnChanged([this](int v) {
        m_settings.fontSize = v;
        updatePreview();
    });

    connect(boldSwitch, &QAbstractButton::toggled, this, [this](bool b) {
        m_settings.isBold = b;
        updatePreview();
    });

    lineSpacingStepper->setOnChanged([this](int v) {
        m_settings.lineSpacing = v;
        updatePreview();
    });

    connect(keepFormatSwitch, &QAbstractButton::toggled, this, [this](bool b) {
        m_settings.keepSourceFormatting = b;
    });

    connect(m_fontFamilyCombo, &QComboBox::currentTextChanged, this, [this](const QString& family) {
        if (!family.isEmpty())
            m_settings.fontFamily = family;
    });

    connect(m_textWindowStyleCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) {
                m_settings.textWindowStyle = m_textWindowStyleCombo->currentData().toInt();
            });

    connect(m_showSourceSwitch, &QAbstractButton::toggled, this, [this](bool b) {
        m_settings.textWindowShowSource = b;
    });

    connect(alignCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        m_settings.textAlignment = idx;
        updatePreview();
    });

    connect(autoClearSwitch, &QAbstractButton::toggled, this, [this](bool b) {
        m_settings.autoClearWindow = b;
    });

    connect(excludeCaptureSwitch, &QAbstractButton::toggled, this, [this](bool b) {
        m_settings.excludeFromCapture = b;
    });

    connect(opacitySlider, &QSlider::valueChanged, this, [this, opValLabel](int v) {
        m_settings.windowOpacity = v;
        opValLabel->setText(QString("%1%").arg(v));
        updatePreview();
    });

    connect(previewBtn, &QPushButton::clicked, this, [this]() {
        m_previewFrame->setVisible(!m_previewFrame->isVisible());
    });

    updatePreview();
    return page;
}

QWidget* SettingsDialog::buildOcrPage()
{
    auto* page = makePage();
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(28, 24, 28, 24);
    lay->setSpacing(14);

    lay->addWidget(makePageTitle("OCR"));
    lay->addWidget(makeDivider());
    lay->addSpacing(4);

    auto makeItemLabel = [](const QString& text) {
        auto* l = new QLabel(text);
        l->setStyleSheet("color: #e2e8f0; font-size: 10.5pt; font-weight: 500;");
        return l;
    };
    auto makeMutedLabel = [](const QString& text) {
        auto* l = new QLabel(text);
        l->setStyleSheet("color: #94a3b8; font-size: 9pt;");
        l->setWordWrap(true);
        return l;
    };

    auto* engineRow = new QHBoxLayout();
    engineRow->setSpacing(16);
    auto* engineValue = new QLabel(QStringLiteral("PaddleOCR · PP-OCRv4"), page);
    engineValue->setStyleSheet(
        "color: #7dd3fc; font-size: 10.5pt; font-weight: 600;");
    engineRow->addWidget(engineValue);
    engineRow->addWidget(makeItemLabel("Engine OCR (tích hợp sẵn)"));
    engineRow->addStretch();
    lay->addLayout(engineRow);

    m_ocrModelLabel = makeMutedLabel(QString());
    lay->addWidget(m_ocrModelLabel);

    auto* gpuRow = new QHBoxLayout();
    gpuRow->setSpacing(12);
    m_ocrGpu = new ToggleSwitch(page);
    m_ocrGpu->setChecked(m_settings.ocrUseGpu);
    gpuRow->addWidget(m_ocrGpu);
    gpuRow->addWidget(makeItemLabel("Dùng GPU (DirectML)"));
    gpuRow->addStretch();
    lay->addLayout(gpuRow);
    lay->addWidget(makeMutedLabel(
        "Tắt để dùng CPU. Nếu DirectML không khả dụng, app tự fallback CPU."));

    auto* binarizeRow = new QHBoxLayout();
    binarizeRow->setSpacing(12);
    m_ocrBinarize = new ToggleSwitch(page);
    m_ocrBinarize->setChecked(m_settings.ocrBinarize);
    binarizeRow->addWidget(m_ocrBinarize);
    binarizeRow->addWidget(makeItemLabel("Nhị phân hóa ảnh trước khi OCR"));
    binarizeRow->addStretch();
    lay->addLayout(binarizeRow);
    lay->addWidget(makeMutedLabel(
        "Chỉ bật khi ảnh nền phức tạp hoặc chữ quá mờ. Bình thường nên tắt."));

    lay->addSpacing(6);
    lay->addWidget(makeMutedLabel(
        "Model OCR được nạp khi bắt đầu dịch. Đổi ngôn ngữ nguồn sẽ tự chọn "
        "model nhận dạng phù hợp (EN / JA / ZH)."));

    connect(m_ocrGpu, &QAbstractButton::toggled, this, [this](bool enabled) {
        m_settings.ocrUseGpu = enabled;
    });
    connect(m_ocrBinarize, &QAbstractButton::toggled, this, [this](bool enabled) {
        m_settings.ocrBinarize = enabled;
    });

    updateOcrInfo();
    lay->addStretch();
    return page;
}

QWidget* SettingsDialog::buildHotkeysPage()
{
    auto* page = makePage();
    auto* lay  = new QVBoxLayout(page);
    lay->setContentsMargins(28, 24, 28, 24);
    lay->setSpacing(16);

    lay->addWidget(makePageTitle("Phím tắt"));
    lay->addWidget(makeDivider());
    lay->addSpacing(6);

    struct HotkeyRow { QString label; QString defaultKey; };
    const QList<HotkeyRow> rows = {
        {"Bắt đầu / Dừng dịch",         "F8"},
        {"Chọn vùng dịch",              "F6"},
        {"Xem / Ẩn khung vùng dịch",    "F7"},
        {"Mở cửa sổ Cài đặt",           "Ctrl + ,"},
    };

    for (const auto& row : rows) {
        auto* rowLay = new QHBoxLayout();
        rowLay->setSpacing(16);

        auto* keyEdit = new QComboBox(page);
        keyEdit->setFixedWidth(180);
        keyEdit->setFixedHeight(36);
        keyEdit->addItems({row.defaultKey, "(Nhấn để gán phím mới)"});
        keyEdit->setStyleSheet(R"(
            QComboBox {
                background-color: #0b1320;
                border: 1px solid #334155;
                border-radius: 6px;
                color: #f8fafc;
                padding: 4px 12px;
                font-size: 10pt;
                font-weight: bold;
            }
            QComboBox:hover { border-color: #38bdf8; }
            QComboBox::drop-down { border: none; width: 24px; }
        )");

        auto* label = new QLabel(row.label, page);
        label->setStyleSheet("color: #e2e8f0; font-size: 10.5pt; font-weight: 500;");

        rowLay->addWidget(keyEdit);
        rowLay->addWidget(label);
        rowLay->addStretch();

        lay->addLayout(rowLay);
        lay->addSpacing(2);
    }

    lay->addStretch();
    return page;
}
