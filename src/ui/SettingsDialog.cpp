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
#include <QGridLayout>
#include <QGroupBox>
#include <QColorDialog>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPainter>
#include <QScrollArea>
#include <QMouseEvent>
#include <QMessageBox>
#include <QProcess>
#include <QFileInfo>
#include <QCoreApplication>
#include <QDir>
#include "core/CudaOcrPack.h"
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
        setProperty("ezStepper", true);
        setFixedSize(104, 32);
        setAttribute(Qt::WA_StyledBackground, true);
        setStyleSheet(R"(
            QWidget[ezStepper="true"] {
                background: #0b1320;
                border: 1px solid #334155;
                border-radius: 6px;
            }
        )");

        auto* lay = new QHBoxLayout(this);
        lay->setContentsMargins(2, 2, 2, 2);
        lay->setSpacing(0);

        const QString btnStyle = R"(
            QPushButton {
                background: transparent;
                color: #94a3b8;
                border: none;
                border-radius: 4px;
                font-size: 11pt;
                font-weight: 600;
            }
            QPushButton:hover { background: #1e293b; color: #38bdf8; }
            QPushButton:pressed { background: #0f172a; color: #0284c7; }
        )";

        m_downBtn = new QPushButton("−", this);
        m_downBtn->setFixedSize(26, 26);
        m_downBtn->setStyleSheet(btnStyle);
        m_downBtn->setCursor(Qt::PointingHandCursor);

        m_label = new QLabel(QString::number(m_val), this);
        m_label->setAlignment(Qt::AlignCenter);
        m_label->setStyleSheet(
            "color: #f8fafc; font-size: 10pt; font-weight: 600; background: transparent;");

        m_upBtn = new QPushButton("+", this);
        m_upBtn->setFixedSize(26, 26);
        m_upBtn->setStyleSheet(btnStyle);
        m_upBtn->setCursor(Qt::PointingHandCursor);

        lay->addWidget(m_downBtn);
        lay->addWidget(m_label, 1);
        lay->addWidget(m_upBtn);

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
    setMinimumSize(800, 600);
    resize(880, 680);
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
    m_stack->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
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
    scroll->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setStyleSheet("QScrollArea { background: transparent; border: none; }");

    auto* content = new QWidget();
    content->setStyleSheet("background: transparent;");
    content->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
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

    lay->addSpacing(10);
    lay->addWidget(makeSectionLabel("Thiết bị dịch (CTranslate2)"));
    m_translateDevice = new QComboBox(content);
    m_translateDevice->setFixedHeight(36);
    m_translateDevice->setStyleSheet(comboStyle);
    m_translateDevice->addItem("Tự động (CUDA → CPU)", "auto");
    m_translateDevice->addItem("CPU", "cpu");
    m_translateDevice->addItem("CUDA", "cuda");
    lay->addWidget(m_translateDevice);
    lay->addWidget(makeHint(
        "Tự động dùng CUDA nếu máy có, ngược lại CPU. "
        "Chỉ áp dụng cho engine offline CTranslate2; Marian/Google/AI không đổi."));

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
    connect(m_translateDevice, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int index) {
                if (!m_translateDevice || index < 0)
                    return;
                m_settings.translateDevice = m_translateDevice->itemData(index).toString();
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
    m_loadingSettings = true;
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
    if (auto* toggle = findChild<ToggleSwitch*>(QStringLiteral("keepFormatSwitch")))
        toggle->setChecked(m_settings.keepSourceFormatting);
    if (auto* toggle = findChild<ToggleSwitch*>(QStringLiteral("autoClearSwitch")))
        toggle->setChecked(m_settings.autoClearWindow);
    if (auto* toggle = findChild<ToggleSwitch*>(QStringLiteral("excludeCaptureSwitch")))
        toggle->setChecked(m_settings.excludeFromCapture);
    if (auto* widget = findChild<QWidget*>(QStringLiteral("lineSpacingStepper"))) {
        if (auto* stepper = dynamic_cast<CompactStepper*>(widget))
            stepper->setValue(m_settings.lineSpacing);
    }
    if (auto* combo = findChild<QComboBox*>(QStringLiteral("alignCombo")))
        combo->setCurrentIndex(qBound(0, m_settings.textAlignment, 2));
    if (auto* slider = findChild<QSlider*>(QStringLiteral("opacitySlider")))
        slider->setValue(m_settings.windowOpacity);
    if (m_ocrDetDevice) {
        const int index = m_ocrDetDevice->findData(m_settings.ocrDetDevice);
        m_ocrDetDevice->setCurrentIndex(index >= 0 ? index : 0);
    }
    if (m_ocrRecDevice) {
        const int index = m_ocrRecDevice->findData(m_settings.ocrRecDevice);
        m_ocrRecDevice->setCurrentIndex(index >= 0 ? index : 0);
    }
    if (m_translateDevice) {
        const int index = m_translateDevice->findData(m_settings.translateDevice);
        m_translateDevice->setCurrentIndex(index >= 0 ? index : 0);
    }
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
    m_loadingSettings = false;
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

    QFont font(m_settings.fontFamily.isEmpty() ? QStringLiteral("Segoe UI")
                                               : m_settings.fontFamily,
               m_settings.fontSize);
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
    mainLay->setContentsMargins(28, 18, 28, 14);
    mainLay->setSpacing(10);
    mainLay->addWidget(makePageTitle("Giao diện"));
    mainLay->addWidget(makeDivider());
    mainLay->addSpacing(2);

    auto* scroll = new QScrollArea(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setStyleSheet("QScrollArea { background: transparent; border: none; }");

    auto* content = new QWidget();
    content->setStyleSheet("background: transparent;");
    content->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto* columns = new QHBoxLayout(content);
    columns->setContentsMargins(0, 2, 8, 0);
    columns->setSpacing(26);

    auto* left = new QVBoxLayout();
    left->setSpacing(8);
    auto* right = new QVBoxLayout();
    right->setSpacing(8);

    auto makeSection = [](const QString& text) {
        auto* label = new QLabel(text);
        label->setStyleSheet("color: #38bdf8; font-size: 9.5pt; font-weight: 700;");
        return label;
    };
    auto makeForm = []() {
        auto* form = new QFormLayout();
        form->setContentsMargins(0, 0, 0, 0);
        form->setHorizontalSpacing(16);
        form->setVerticalSpacing(9);
        form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
        form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
        form->setRowWrapPolicy(QFormLayout::DontWrapRows);
        return form;
    };
    auto addRow = [](QFormLayout* form, const QString& text, QWidget* field) {
        auto* label = new QLabel(text);
        label->setStyleSheet("color: #cbd5e1; font-size: 9.5pt;");
        form->addRow(label, field);
    };
    auto makeToggleCell = [](ToggleSwitch* toggle, const QString& text) {
        auto* cell = new QWidget();
        auto* h = new QHBoxLayout(cell);
        h->setContentsMargins(0, 2, 0, 2);
        h->setSpacing(8);
        h->addWidget(toggle);
        auto* label = new QLabel(text, cell);
        label->setStyleSheet("color: #cbd5e1; font-size: 9.5pt;");
        h->addWidget(label);
        h->addStretch(1);
        return cell;
    };

    const QString comboStyle = R"(
        QComboBox {
            background-color: #0b1320;
            border: 1px solid #334155;
            border-radius: 6px;
            color: #f8fafc;
            padding: 3px 10px;
            font-size: 9.5pt;
        }
        QComboBox:hover { border-color: #38bdf8; }
        QComboBox::drop-down { border: none; width: 22px; }
        QComboBox QAbstractItemView {
            background-color: #0f172a;
            color: #f8fafc;
            selection-background-color: #1e3a8a;
            border: 1px solid #334155;
            border-radius: 6px;
            padding: 4px;
        }
    )";

    // ── Cột trái: màu sắc, chữ, hiển thị ──────────────────────────────────
    left->addWidget(makeSection("Màu sắc & chữ"));

    auto* winColorBtn = new ColorCircleButton(m_settings.windowColor, content);
    winColorBtn->setObjectName(QStringLiteral("winColorBtn"));

    auto* fontColorBtn = new ColorCircleButton(m_settings.fontColor, content);
    fontColorBtn->setObjectName(QStringLiteral("fontColorBtn"));

    m_fontFamilyCombo = new QComboBox(content);
    m_fontFamilyCombo->addItems({"Segoe UI", "Arial", "Tahoma", "Times New Roman", "Consolas"});
    m_fontFamilyCombo->setCurrentText(m_settings.fontFamily);
    m_fontFamilyCombo->setFixedHeight(32);
    m_fontFamilyCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_fontFamilyCombo->setStyleSheet(comboStyle);

    auto* fontSizeStepper = new CompactStepper(8, 72, m_settings.fontSize, content);
    fontSizeStepper->setObjectName(QStringLiteral("fontSizeStepper"));

    auto* alignCombo = new QComboBox(content);
    alignCombo->setObjectName(QStringLiteral("alignCombo"));
    alignCombo->addItems({"Trái", "Giữa", "Phải"});
    alignCombo->setCurrentIndex(qBound(0, m_settings.textAlignment, 2));
    alignCombo->setFixedHeight(32);
    alignCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    alignCombo->setStyleSheet(comboStyle);

    auto* textForm = makeForm();
    addRow(textForm, "Màu nền", winColorBtn);
    addRow(textForm, "Màu chữ", fontColorBtn);
    addRow(textForm, "Font chữ", m_fontFamilyCombo);
    addRow(textForm, "Cỡ chữ", fontSizeStepper);
    addRow(textForm, "Căn lề", alignCombo);
    left->addLayout(textForm);

    left->addSpacing(10);
    left->addWidget(makeSection("Hiển thị bản dịch"));

    m_textWindowStyleCombo = new QComboBox(content);
    m_textWindowStyleCombo->addItem("Solid", 0);
    m_textWindowStyleCombo->addItem("Glass", 1);
    m_textWindowStyleCombo->addItem("Outline", 2);
    m_textWindowStyleCombo->addItem("Paper", 3);
    m_textWindowStyleCombo->setCurrentIndex(
        qBound(0, m_textWindowStyleCombo->findData(m_settings.textWindowStyle), 3));
    m_textWindowStyleCombo->setFixedHeight(32);
    m_textWindowStyleCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_textWindowStyleCombo->setStyleSheet(comboStyle);

    auto* lineSpacingStepper = new CompactStepper(6, 60, m_settings.lineSpacing, content);
    lineSpacingStepper->setObjectName(QStringLiteral("lineSpacingStepper"));

    auto* displayForm = makeForm();
    addRow(displayForm, "Kiểu nền", m_textWindowStyleCombo);
    addRow(displayForm, "Giãn dòng", lineSpacingStepper);
    left->addLayout(displayForm);

    left->addSpacing(4);

    auto* boldSwitch = new ToggleSwitch(content);
    boldSwitch->setObjectName(QStringLiteral("boldSwitch"));
    boldSwitch->setChecked(m_settings.isBold);

    m_showSourceSwitch = new ToggleSwitch(content);
    m_showSourceSwitch->setChecked(m_settings.textWindowShowSource);

    auto* keepFormatSwitch = new ToggleSwitch(content);
    keepFormatSwitch->setObjectName(QStringLiteral("keepFormatSwitch"));
    keepFormatSwitch->setChecked(m_settings.keepSourceFormatting);

    auto* autoClearSwitch = new ToggleSwitch(content);
    autoClearSwitch->setObjectName(QStringLiteral("autoClearSwitch"));
    autoClearSwitch->setChecked(m_settings.autoClearWindow);

    m_novelModeSwitch = new ToggleSwitch(content);
    m_novelModeSwitch->setChecked(m_settings.novelMode);
    m_novelModeSwitch->setToolTip(
        "Luôn dùng layout đọc truyện (đoạn văn + nền giấy), không tự chuyển sang UI.");

    m_gameDialogModeSwitch = new ToggleSwitch(content);
    m_gameDialogModeSwitch->setObjectName(QStringLiteral("gameDialogModeSwitch"));
    m_gameDialogModeSwitch->setChecked(m_settings.gameDialogMode);
    m_gameDialogModeSwitch->setToolTip(
        "So sánh vùng chữ đã nhận diện để bỏ qua nền game chuyển động. Hãy chọn ROI gọn quanh hộp thoại.");

    auto* toggles = new QGridLayout();
    toggles->setContentsMargins(0, 0, 0, 0);
    toggles->setHorizontalSpacing(20);
    toggles->setVerticalSpacing(2);
    toggles->setColumnStretch(0, 1);
    toggles->setColumnStretch(1, 1);
    toggles->addWidget(makeToggleCell(boldSwitch, "In đậm"), 0, 0);
    toggles->addWidget(makeToggleCell(m_showSourceSwitch, "Hiện bản gốc"), 0, 1);
    toggles->addWidget(makeToggleCell(keepFormatSwitch, "Giữ định dạng nguồn"), 1, 0);
    toggles->addWidget(makeToggleCell(autoClearSwitch, "Tự xoá cửa sổ"), 1, 1);
    toggles->addWidget(makeToggleCell(m_novelModeSwitch, "Novel mode"), 2, 0);
    toggles->addWidget(makeToggleCell(m_gameDialogModeSwitch, "Game dialogue"), 2, 1);
    left->addLayout(toggles);
    left->addStretch(1);

    // ── Cột phải: xem trước + cửa sổ ──────────────────────────────────────
    right->addWidget(makeSection("Xem trước"));

    m_previewFrame = new QFrame(content);
    m_previewFrame->setObjectName(QStringLiteral("previewFrame"));
    m_previewFrame->setMinimumHeight(190);
    m_previewFrame->setMaximumHeight(250);
    auto* pfLay = new QVBoxLayout(m_previewFrame);
    pfLay->setContentsMargins(14, 11, 14, 11);
    pfLay->setSpacing(6);

    auto* sampleTag = new QLabel("XEM TRƯỚC BẢN DỊCH", m_previewFrame);
    sampleTag->setStyleSheet(
        "color: #64748b; font-size: 7pt; font-weight: bold; letter-spacing: 1px;");
    pfLay->addWidget(sampleTag);

    m_previewTextLabel = new QLabel(
        "Chào mừng bạn đến với EZ-Translator!\n"
        "Welcome to EZ-Translator real-time subtitle.",
        m_previewFrame);
    m_previewTextLabel->setWordWrap(true);
    pfLay->addWidget(m_previewTextLabel, 1);
    right->addWidget(m_previewFrame);

    right->addSpacing(23);
    right->addWidget(makeSection("Cửa sổ & overlay"));

    auto* opacityRow = new QWidget(content);
    auto* opacityLay = new QVBoxLayout(opacityRow);
    opacityLay->setContentsMargins(0, 0, 0, 0);
    opacityLay->setSpacing(6);
    auto* opHead = new QHBoxLayout();
    auto* opLabel = new QLabel("Độ mờ cửa sổ", opacityRow);
    opLabel->setStyleSheet("color: #cbd5e1; font-size: 9.5pt;");
    opHead->addWidget(opLabel);
    opHead->addStretch(1);
    auto* opValLabel = new QLabel(QString("%1%").arg(m_settings.windowOpacity), opacityRow);
    opValLabel->setStyleSheet("color: #38bdf8; font-size: 9.5pt; font-weight: bold;");
    opHead->addWidget(opValLabel);
    opacityLay->addLayout(opHead);

    auto* opacitySlider = new QSlider(Qt::Horizontal, opacityRow);
    opacitySlider->setObjectName(QStringLiteral("opacitySlider"));
    opacitySlider->setRange(10, 100);
    opacitySlider->setValue(m_settings.windowOpacity);
    opacitySlider->setStyleSheet(R"(
        QSlider::groove:horizontal { height: 6px; background: #1e293b; border-radius: 3px; }
        QSlider::sub-page:horizontal { background: #38bdf8; border-radius: 3px; }
        QSlider::handle:horizontal {
            background: #e0f2fe; border: 2px solid #0284c7;
            width: 16px; margin-top: -5px; margin-bottom: -5px; border-radius: 8px;
        }
        QSlider::handle:horizontal:hover { background: #ffffff; border-color: #38bdf8; }
    )");
    opacityLay->addWidget(opacitySlider);
    right->addWidget(opacityRow);

    auto* excludeCaptureSwitch = new ToggleSwitch(content);
    excludeCaptureSwitch->setObjectName(QStringLiteral("excludeCaptureSwitch"));
    excludeCaptureSwitch->setChecked(m_settings.excludeFromCapture);
    right->addWidget(makeToggleCell(excludeCaptureSwitch, "Loại khỏi capture"));

    right->addStretch(1);

    columns->addLayout(left, 3);
    columns->addLayout(right, 2);
    scroll->setWidget(content);
    mainLay->addWidget(scroll, 1);

    // ── Wiring ─────────────────────────────────────────────────────────────
    connect(winColorBtn, &QPushButton::clicked, this, [this, winColorBtn]() {
        QColor chosen = QColorDialog::getColor(m_settings.windowColor, this,
                                               "Chọn màu nền cửa sổ", QColorDialog::ShowAlphaChannel);
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
        if (!family.isEmpty()) {
            m_settings.fontFamily = family;
            updatePreview();
        }
    });
    connect(m_textWindowStyleCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) {
                m_settings.textWindowStyle = m_textWindowStyleCombo->currentData().toInt();
            });
    connect(m_showSourceSwitch, &QAbstractButton::toggled, this, [this](bool b) {
        m_settings.textWindowShowSource = b;
    });
    connect(alignCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int idx) {
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
    connect(m_novelModeSwitch, &QAbstractButton::toggled, this, [this](bool enabled) {
        m_settings.novelMode = enabled;
    });
    connect(m_gameDialogModeSwitch, &QAbstractButton::toggled, this, [this](bool enabled) {
        m_settings.gameDialogMode = enabled;
    });

    updatePreview();
    return page;
}


QWidget* SettingsDialog::buildOcrPage()
{
    auto* page = makePage();
    auto* outer = new QVBoxLayout(page);
    outer->setContentsMargins(28, 24, 28, 18);
    outer->setSpacing(10);
    outer->addWidget(makePageTitle("OCR"));
    outer->addWidget(makeDivider());
    outer->addSpacing(4);

    auto* scroll = new QScrollArea(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setStyleSheet("QScrollArea { background: transparent; border: none; }");

    auto* content = new QWidget();
    content->setStyleSheet("background: transparent;");
    content->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* lay = new QVBoxLayout(content);
    lay->setContentsMargins(0, 4, 10, 0);
    lay->setSpacing(12);

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

    const QString ocrComboStyle = R"(
        QComboBox {
            background-color: #0b1320;
            border: 1px solid #334155;
            border-radius: 6px;
            color: #f8fafc;
            padding: 4px 10px;
            font-size: 10pt;
        }
        QComboBox:hover { border-color: #38bdf8; }
        QComboBox QAbstractItemView {
            background-color: #0f172a;
            color: #f8fafc;
            selection-background-color: #1e3a8a;
        }
    )";
    lay->addWidget(makeItemLabel("Thiết bị OCR nâng cao"));
    auto* detRow = new QHBoxLayout();
    detRow->setSpacing(12);
    detRow->addWidget(makeItemLabel("Detection"));
    m_ocrDetDevice = new QComboBox(page);
    m_ocrDetDevice->setStyleSheet(ocrComboStyle);
    m_ocrDetDevice->addItem("Tự động (CUDA → DML → CPU)", "auto");
    m_ocrDetDevice->addItem("CPU", "cpu");
    m_ocrDetDevice->addItem("DirectML", "directml");
    m_ocrDetDevice->addItem("CUDA (cần gói OCR CUDA)", "cuda");
    detRow->addWidget(m_ocrDetDevice, 1);
    lay->addLayout(detRow);

    auto* recRow = new QHBoxLayout();
    recRow->setSpacing(12);
    recRow->addWidget(makeItemLabel("Recognition"));
    m_ocrRecDevice = new QComboBox(page);
    m_ocrRecDevice->setStyleSheet(ocrComboStyle);
    m_ocrRecDevice->addItem("Tự động (CUDA → DML → CPU)", "auto");
    m_ocrRecDevice->addItem("CPU", "cpu");
    m_ocrRecDevice->addItem("DirectML", "directml");
    m_ocrRecDevice->addItem("CUDA (cần gói OCR CUDA)", "cuda");
    recRow->addWidget(m_ocrRecDevice, 1);
    lay->addLayout(recRow);
    lay->addWidget(makeMutedLabel(
        "Auto ưu tiên CUDA, rồi DirectML, cuối cùng CPU. "
        "CUDA OCR cần gói ONNX Runtime CUDA cài thêm; nếu thiếu app tự fallback."));

    m_cudaPackStatus = makeMutedLabel(QString());
    lay->addWidget(m_cudaPackStatus);
    m_cudaInstallBtn = new SecondaryButton("Cài gói CUDA OCR...", page);
    lay->addWidget(m_cudaInstallBtn);

    const auto startCudaInstall = [this]() -> bool {
        if (EZTranslator::CudaOcrPack::isInstalled()) {
            QMessageBox::information(this, "CUDA OCR",
                                     "Gói CUDA OCR đã được cài.");
            return true;
        }
        const auto answer = QMessageBox::question(
            this, "Cài CUDA OCR",
            "CUDA OCR cần tải thêm ONNX Runtime CUDA (~750 MB–1 GB) và cần "
            "CUDA/cuDNN có sẵn trên máy.\n\n"
            "Script sẽ cài vào thư mục người dùng, không cần quyền admin.\n"
            "Tiếp tục tải và cài?",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return false;

        const QString script = EZTranslator::CudaOcrPack::installScriptPath();
        if (!QFileInfo::exists(script)) {
            QMessageBox::warning(this, "CUDA OCR",
                                 "Không tìm thấy script cài đặt:\n" + script);
            return false;
        }
        QStringList args = {"-NoProfile", "-ExecutionPolicy", "Bypass", "-File", script,
                            "-BaseAppDir", QCoreApplication::applicationDirPath(),
                            "-ProfileDir", EZTranslator::CudaOcrPack::profileDir()};
        const QString models = QDir(QCoreApplication::applicationDirPath())
                                   .filePath(QStringLiteral("models"));
        if (QDir(models).exists()) {
            args << "-ModelsDir" << models;
        }
        const bool started = QProcess::startDetached(QStringLiteral("powershell.exe"), args);
        if (!started) {
            QMessageBox::warning(this, "CUDA OCR",
                                 "Không chạy được script cài đặt PowerShell.");
            return false;
        }
        QMessageBox::information(
            this, "CUDA OCR",
            "Đang chạy script cài đặt trong cửa sổ PowerShell.\n"
            "Khi script hoàn tất, hãy khởi động lại EZTranslator để dùng CUDA OCR.");
        return true;
    };

    const auto updateCudaStatus = [this]() {
        if (!m_cudaPackStatus)
            return;
        m_cudaPackStatus->setText(
            EZTranslator::CudaOcrPack::isInstalled()
                ? QStringLiteral("Gói CUDA OCR: đã cài. Khởi động lại app sau khi đổi thiết bị.")
                : QStringLiteral("Gói CUDA OCR: chưa cài. Bấm nút bên dưới để tải."));
    };
    updateCudaStatus();
    connect(m_cudaInstallBtn, &QPushButton::clicked, this, [startCudaInstall, updateCudaStatus]() {
        startCudaInstall();
        updateCudaStatus();
    });

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

    connect(m_ocrDetDevice, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, startCudaInstall](int index) {
                if (m_loadingSettings || !m_ocrDetDevice || index < 0)
                    return;
                const QString device = m_ocrDetDevice->itemData(index).toString();
                if (device == QLatin1String("cuda") && !EZTranslator::CudaOcrPack::isInstalled()
                    && !startCudaInstall()) {
                    QSignalBlocker blocker(m_ocrDetDevice);
                    m_ocrDetDevice->setCurrentIndex(0);
                    m_settings.ocrDetDevice = QStringLiteral("auto");
                    return;
                }
                m_settings.ocrDetDevice = device;
            });
    connect(m_ocrRecDevice, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, startCudaInstall](int index) {
                if (m_loadingSettings || !m_ocrRecDevice || index < 0)
                    return;
                const QString device = m_ocrRecDevice->itemData(index).toString();
                if (device == QLatin1String("cuda") && !EZTranslator::CudaOcrPack::isInstalled()
                    && !startCudaInstall()) {
                    QSignalBlocker blocker(m_ocrRecDevice);
                    m_ocrRecDevice->setCurrentIndex(0);
                    m_settings.ocrRecDevice = QStringLiteral("auto");
                    return;
                }
                m_settings.ocrRecDevice = device;
            });
    connect(m_ocrBinarize, &QAbstractButton::toggled, this, [this](bool enabled) {
        m_settings.ocrBinarize = enabled;
    });

    updateOcrInfo();
    lay->addStretch();
    scroll->setWidget(content);
    outer->addWidget(scroll, 1);
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
