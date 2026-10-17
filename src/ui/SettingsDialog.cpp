#include "SettingsDialog.h"

#include "ui/components/PrimaryButton.h"
#include "ui/components/SecondaryButton.h"
#include "ui/components/ToggleSwitch.h"
#include "ui/theme/StyleTheme.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

// Hộp chọn màu với hai nút có nền, viền và khoảng cách rõ ràng.
static QColor chooseOverlayColor(
    const QColor &initialColor,
    QWidget *parent,
    const QString &title)
{
    QColorDialog dialog(initialColor, parent);

    dialog.setOption(QColorDialog::DontUseNativeDialog, true);
    dialog.setWindowTitle(title);

    auto *buttonBox = dialog.findChild<QDialogButtonBox *>();

    if (buttonBox)
    {
        if (buttonBox->layout())
            buttonBox->layout()->setSpacing(12);

        if (auto *okButton =
                buttonBox->button(QDialogButtonBox::Ok))
        {
            okButton->setObjectName("colorAcceptButton");
            okButton->setText("Chọn màu");
            okButton->setMinimumSize(110, 40);
            okButton->setCursor(Qt::PointingHandCursor);
        }

        if (auto *cancelButton =
                buttonBox->button(QDialogButtonBox::Cancel))
        {
            cancelButton->setObjectName("colorCancelButton");
            cancelButton->setText("Hủy");
            cancelButton->setMinimumSize(110, 40);
            cancelButton->setCursor(Qt::PointingHandCursor);
        }
    }

    dialog.setStyleSheet(R"(
        QPushButton#colorAcceptButton,
        QPushButton#colorCancelButton {
            color: #ffffff;
            border-radius: 8px;
            padding: 0px 16px;
            font-size: 14px;
            font-weight: 600;
        }

        QPushButton#colorAcceptButton {
            background-color: #2563eb;
            border: 1px solid #60a5fa;
        }

        QPushButton#colorAcceptButton:hover {
            background-color: #3b82f6;
        }

        QPushButton#colorAcceptButton:pressed {
            background-color: #1d4ed8;
        }

        QPushButton#colorCancelButton {
            background-color: #334155;
            border: 1px solid #94a3b8;
        }

        QPushButton#colorCancelButton:hover {
            background-color: #475569;
        }

        QPushButton#colorCancelButton:pressed {
            background-color: #1e293b;
        }

        QPushButton#colorAcceptButton:focus,
        QPushButton#colorCancelButton:focus {
            border: 2px solid #ffffff;
        }
    )");

    if (dialog.exec() == QDialog::Accepted)
        return dialog.selectedColor();

    return QColor();
}

namespace
{

    class ColorCircleButton : public QPushButton
    {
    public:
        explicit ColorCircleButton(
            const QColor &initialColor,
            QWidget *parent = nullptr)
            : QPushButton(parent),
              m_color(initialColor)
        {
            setFixedSize(36, 36);
            setCursor(Qt::PointingHandCursor);

            setStyleSheet(
                "QPushButton {"
                "border: none;"
                "background: transparent;"
                "}");
        }

        QColor color() const
        {
            return m_color;
        }

        void setColor(const QColor &color)
        {
            m_color = color;
            update();
        }

    protected:
        void paintEvent(QPaintEvent *) override
        {
            QPainter painter(this);
            painter.setRenderHint(QPainter::Antialiasing);

            QRect circleRect = rect().adjusted(3, 3, -3, -3);

            painter.setPen(QPen(QColor("#64748b"), 2.0f));
            painter.setBrush(m_color);
            painter.drawEllipse(circleRect);

            if (m_color.lightness() > 200)
            {
                painter.setPen(QPen(QColor(0, 0, 0, 50), 1.0f));
                painter.setBrush(Qt::NoBrush);
                painter.drawEllipse(
                    circleRect.adjusted(1, 1, -1, -1));
            }
        }

    private:
        QColor m_color;
    };

    class CompactStepper : public QWidget
    {
    public:
        explicit CompactStepper(
            int minVal,
            int maxVal,
            int initialVal,
            QWidget *parent = nullptr)
            : QWidget(parent),
              m_min(minVal),
              m_max(maxVal),
              m_val(initialVal)
        {
            setFixedWidth(34);

            auto *layout = new QVBoxLayout(this);
            layout->setContentsMargins(0, 0, 0, 0);
            layout->setSpacing(0);

            m_upBtn = new QPushButton("▲", this);
            m_upBtn->setFixedSize(30, 14);
            m_upBtn->setStyleSheet(R"(
            QPushButton {
                background: transparent;
                color: #94a3b8;
                border: none;
                font-size: 7.5pt;
                font-weight: bold;
            }
            QPushButton:hover {
                color: #38bdf8;
            }
            QPushButton:pressed {
                color: #0284c7;
            }
        )");

            m_label = new QLabel(QString::number(m_val), this);
            m_label->setAlignment(Qt::AlignCenter);
            m_label->setStyleSheet(
                "color: #f8fafc;"
                "font-size: 11.5pt;"
                "font-weight: bold;"
                "background: transparent;");

            m_downBtn = new QPushButton("▼", this);
            m_downBtn->setFixedSize(30, 14);
            m_downBtn->setStyleSheet(R"(
            QPushButton {
                background: transparent;
                color: #94a3b8;
                border: none;
                font-size: 7.5pt;
                font-weight: bold;
            }
            QPushButton:hover {
                color: #38bdf8;
            }
            QPushButton:pressed {
                color: #0284c7;
            }
        )");

            layout->addWidget(m_upBtn, 0, Qt::AlignCenter);
            layout->addWidget(m_label, 0, Qt::AlignCenter);
            layout->addWidget(m_downBtn, 0, Qt::AlignCenter);

            connect(m_upBtn, &QPushButton::clicked, this, [this]()
                    {
            if (m_val < m_max) {
                ++m_val;
                m_label->setText(QString::number(m_val));

                if (m_onChanged)
                    m_onChanged(m_val);
            } });

            connect(m_downBtn, &QPushButton::clicked, this, [this]()
                    {
            if (m_val > m_min) {
                --m_val;
                m_label->setText(QString::number(m_val));

                if (m_onChanged)
                    m_onChanged(m_val);
            } });
        }

        int value() const
        {
            return m_val;
        }

        void setValue(int value)
        {
            m_val = std::clamp(value, m_min, m_max);
            m_label->setText(QString::number(m_val));

            if (m_onChanged)
                m_onChanged(m_val);
        }

        void setOnChanged(std::function<void(int)> callback)
        {
            m_onChanged = std::move(callback);
        }

    private:
        int m_min;
        int m_max;
        int m_val;

        QPushButton *m_upBtn{nullptr};
        QPushButton *m_downBtn{nullptr};
        QLabel *m_label{nullptr};

        std::function<void(int)> m_onChanged;
    };

} // namespace

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::Dialog);
    setMinimumSize(700, 520);
    resize(760, 560);

    setStyleSheet(
        QString("background-color: %1; border-radius: 12px;")
            .arg(StyleTheme::ColorBackground));

    buildUi();
}

static QLabel *makePageTitle(const QString &title)
{
    auto *label = new QLabel(title);

    label->setStyleSheet(
        QString("color: %1; font-size: 14pt; font-weight: 600;")
            .arg(StyleTheme::ColorTextPrimary));

    return label;
}

static QFrame *makeDivider()
{
    auto *frame = new QFrame();
    frame->setFixedHeight(1);

    frame->setStyleSheet(
        QString("background: %1;")
            .arg(StyleTheme::ColorBorder));

    return frame;
}

static QWidget *makePage()
{
    auto *page = new QWidget();
    page->setStyleSheet("background: transparent;");
    return page;
}

static QLabel *makeFieldLabel(const QString &text)
{
    auto *label = new QLabel(text);

    label->setStyleSheet(
        QString("color: %1; font-size: 10pt;")
            .arg(StyleTheme::ColorTextPrimary));

    return label;
}

void SettingsDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Thanh tiêu đề.
    auto *titleBar = new QWidget(this);
    titleBar->setFixedHeight(50);
    titleBar->setStyleSheet(
        QString("background: %1; border-bottom: 1px solid %2;")
            .arg(StyleTheme::ColorBackground)
            .arg(StyleTheme::ColorBorder));

    auto *titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(20, 0, 16, 0);

    auto *titleLabel = new QLabel("Cài đặt", titleBar);
    titleLabel->setStyleSheet(
        QString("color: %1; font-size: 13pt; font-weight: 600;")
            .arg(StyleTheme::ColorTextPrimary));

    auto *closeButton = new QPushButton("✕", titleBar);
    closeButton->setFixedSize(30, 30);
    closeButton->setStyleSheet(
        QString(R"(
            QPushButton {
                background: transparent;
                color: %1;
                border: none;
                font-size: 12pt;
                border-radius: 6px;
            }
            QPushButton:hover {
                background: #c0392b;
                color: white;
            }
        )")
            .arg(StyleTheme::ColorTextSecondary));

    connect(
        closeButton,
        &QPushButton::clicked,
        this,
        &QDialog::reject);

    titleLayout->addWidget(titleLabel);
    titleLayout->addStretch();
    titleLayout->addWidget(closeButton);
    root->addWidget(titleBar);

    // Nội dung chính.
    auto *body = new QWidget(this);
    auto *bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);

    m_sidebar = new QListWidget(body);
    m_sidebar->setFixedWidth(180);
    m_sidebar->setStyleSheet(
        QString(R"(
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
            QListWidget::item:hover {
                background: %4;
                color: %5;
            }
            QListWidget::item:selected {
                background: %6;
                color: %5;
            }
        )")
            .arg(StyleTheme::ColorBackground)
            .arg(StyleTheme::ColorBorder)
            .arg(StyleTheme::ColorTextSecondary)
            .arg(StyleTheme::ColorSidebarHover)
            .arg(StyleTheme::ColorTextPrimary)
            .arg(StyleTheme::ColorSidebarActive));

    const QStringList sections = {
        "Chung",
        "Giao diện",
        "OCR",
        "Phím tắt"};

    for (const auto &section : sections)
        m_sidebar->addItem(section);

    m_stack = new QStackedWidget(body);
    m_stack->addWidget(buildGeneralPage());
    m_stack->addWidget(buildOverlayPage());
    m_stack->addWidget(buildOcrPage());
    m_stack->addWidget(buildHotkeysPage());

    connect(
        m_sidebar,
        &QListWidget::currentRowChanged,
        m_stack,
        &QStackedWidget::setCurrentIndex);

    m_sidebar->setCurrentRow(0);

    bodyLayout->addWidget(m_sidebar);
    bodyLayout->addWidget(m_stack, 1);
    root->addWidget(body, 1);

    // Các nút cuối cửa sổ.
    auto *footer = new QWidget(this);
    footer->setFixedHeight(56);
    footer->setStyleSheet(
        QString("background: %1; border-top: 1px solid %2;")
            .arg(StyleTheme::ColorSurface)
            .arg(StyleTheme::ColorBorder));

    auto *footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(20, 0, 20, 0);
    footerLayout->setSpacing(10);
    footerLayout->addStretch();

    auto *cancelButton = new SecondaryButton("Hủy", footer);
    cancelButton->setFixedWidth(100);

    connect(
        cancelButton,
        &QPushButton::clicked,
        this,
        &QDialog::reject);

    auto *saveButton = new PrimaryButton("Lưu cài đặt", footer);
    saveButton->setFixedWidth(130);

    connect(saveButton, &QPushButton::clicked, this, [this]()
            {
        if (!saveSettings(m_settings)) {
            QMessageBox::warning(
                this,
                "Không lưu được cài đặt",
                "Chưa lưu được cài đặt. Bạn hãy thử lại.");
            return;
        }

        emit settingsSaved(m_settings);
        accept(); });

    footerLayout->addWidget(cancelButton);
    footerLayout->addWidget(saveButton);
    root->addWidget(footer);
}

QWidget *SettingsDialog::buildGeneralPage()
{
    auto *page = makePage();
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(16);

    layout->addWidget(makePageTitle("Chung"));
    layout->addWidget(makeDivider());
    layout->addSpacing(6);

    auto makeItemLabel = [](const QString &text)
    {
        auto *label = new QLabel(text);
        label->setStyleSheet(
            "color: #e2e8f0;"
            "font-size: 10.5pt;"
            "font-weight: 500;");
        return label;
    };

    auto *translationRow = new QHBoxLayout();
    translationRow->setSpacing(16);

    auto *translationCombo = new QComboBox(page);
    translationCombo->setFixedWidth(180);
    translationCombo->setFixedHeight(36);
    translationCombo->addItems({"Google", "Offline", "DeepL", "Yandex", "Bing"});

    translationCombo->setStyleSheet(R"(
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

    translationRow->addWidget(translationCombo);
    translationRow->addWidget(makeItemLabel("Translator"));
    translationRow->addStretch();
    layout->addLayout(translationRow);

    auto *speechRow = new QHBoxLayout();
    speechRow->setSpacing(16);

    auto *speechCombo = new QComboBox(page);
    speechCombo->setFixedWidth(180);
    speechCombo->setFixedHeight(36);
    speechCombo->addItems({"None", "Windows TTS", "Google TTS"});

    speechCombo->setStyleSheet(R"(
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

    speechRow->addWidget(speechCombo);
    speechRow->addWidget(makeItemLabel("Text to speech system"));
    speechRow->addStretch();
    layout->addLayout(speechRow);

    layout->addSpacing(8);
    layout->addWidget(makeFieldLabel("Ngôn ngữ giao diện"));

    auto *languageCombo = new QComboBox(page);
    languageCombo->setFixedWidth(200);
    languageCombo->addItems({"Tiếng Việt", "English"});

    languageCombo->setStyleSheet(R"(
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
    )");

    layout->addWidget(languageCombo);
    layout->addStretch();

    return page;
}

QWidget *SettingsDialog::buildOcrPage()
{
    auto *page = makePage();
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(16);

    layout->addWidget(makePageTitle("OCR"));
    layout->addWidget(makeDivider());
    layout->addSpacing(6);

    auto makeItemLabel = [](const QString &text)
    {
        auto *label = new QLabel(text);
        label->setStyleSheet(
            "color: #e2e8f0;"
            "font-size: 10.5pt;"
            "font-weight: 500;");
        return label;
    };

    auto *ocrRow = new QHBoxLayout();
    ocrRow->setSpacing(16);

    auto *ocrCombo = new QComboBox(page);
    ocrCombo->setFixedWidth(220);
    ocrCombo->setFixedHeight(36);
    ocrCombo->addItems({"Windows Media OCR",
                        "Tesseract OCR",
                        "PaddleOCR",
                        "EasyOCR"});

    ocrCombo->setStyleSheet(R"(
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

    ocrRow->addWidget(ocrCombo);
    ocrRow->addWidget(makeItemLabel("Engine OCR"));
    ocrRow->addStretch();

    layout->addLayout(ocrRow);
    layout->addSpacing(6);

    auto *binarizeRow = new QHBoxLayout();
    binarizeRow->setSpacing(12);

    auto *binarizeSwitch = new ToggleSwitch(page);
    binarizeSwitch->setChecked(true);

    binarizeRow->addWidget(binarizeSwitch);
    binarizeRow->addWidget(
        makeItemLabel("Khử nhiễu & nhị phân hóa ảnh trước khi quét"));
    binarizeRow->addStretch();

    layout->addLayout(binarizeRow);

    auto *deskewRow = new QHBoxLayout();
    deskewRow->setSpacing(12);

    auto *deskewSwitch = new ToggleSwitch(page);
    deskewSwitch->setChecked(false);

    deskewRow->addWidget(deskewSwitch);
    deskewRow->addWidget(
        makeItemLabel("Tự động cân chỉnh góc xoay chữ"));
    deskewRow->addStretch();

    layout->addLayout(deskewRow);
    layout->addStretch();

    return page;
}

void SettingsDialog::updatePreview()
{
    if (!m_previewFrame || !m_previewTextLabel)
        return;

    QColor base = m_settings.windowColor;

    const int alpha =
        qRound(m_settings.windowOpacity * 255.0 / 100.0);

    base.setAlpha(alpha);

    QColor top = base.lighter(130);
    QColor bottom = base.darker(130);

    const auto rgba = [](const QColor &color)
    {
        return QString("rgba(%1,%2,%3,%4)")
            .arg(color.red())
            .arg(color.green())
            .arg(color.blue())
            .arg(color.alpha());
    };

    m_previewFrame->setStyleSheet(
        QString(
            "QFrame#previewFrame {"
            "background: qlineargradient("
            "x1:0,y1:0,x2:0,y2:1,"
            "stop:0 %1, stop:0.45 %2, stop:1 %3);"
            "border:1px solid #64748b;"
            "border-radius:8px;"
            "}")
            .arg(rgba(top), rgba(base), rgba(bottom)));

    Qt::Alignment alignment = Qt::AlignLeft;

    if (m_settings.textAlignment == 1)
        alignment = Qt::AlignHCenter;
    else if (m_settings.textAlignment == 2)
        alignment = Qt::AlignRight;

    m_previewTextLabel->setAlignment(
        alignment | Qt::AlignVCenter);

    m_previewTextLabel->setStyleSheet(
        QString(
            "color:%1;"
            "background:transparent;"
            "border:none;"
            "font-family:'Segoe UI';"
            "font-size:%2px;"
            "font-weight:%3;")
            .arg(m_settings.fontColor.name())
            .arg(m_settings.fontSize)
            .arg(m_settings.isBold ? 700 : 400));
}

QWidget *SettingsDialog::buildOverlayPage()
{
    auto *page = makePage();
    auto *mainLayout = new QVBoxLayout(page);
    mainLayout->setContentsMargins(28, 20, 28, 20);
    mainLayout->setSpacing(12);

    mainLayout->addWidget(makePageTitle("Giao diện"));
    mainLayout->addWidget(makeDivider());
    mainLayout->addSpacing(4);

    auto *scroll = new QScrollArea(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet(
        "QScrollArea { background: transparent; border: none; }");

    auto *scrollContent = new QWidget();
    scrollContent->setStyleSheet("background: transparent;");

    auto *layout = new QVBoxLayout(scrollContent);
    layout->setContentsMargins(0, 0, 10, 0);
    layout->setSpacing(16);

    auto makeItemLabel = [](const QString &text)
    {
        auto *label = new QLabel(text);
        label->setStyleSheet(
            "color: #e2e8f0;"
            "font-size: 10.5pt;"
            "font-weight: 500;");
        return label;
    };

    // Màu nền và màu chữ.
    auto *row1 = new QHBoxLayout();
    row1->setSpacing(24);

    auto *backgroundColorRow = new QHBoxLayout();
    backgroundColorRow->setSpacing(10);

    auto *backgroundColorButton =
        new ColorCircleButton(
            m_settings.windowColor,
            scrollContent);

    backgroundColorRow->addWidget(backgroundColorButton);
    backgroundColorRow->addWidget(makeItemLabel("Màu nền"));
    backgroundColorRow->addStretch();

    auto *fontColorRow = new QHBoxLayout();
    fontColorRow->setSpacing(10);

    auto *fontColorButton =
        new ColorCircleButton(
            m_settings.fontColor,
            scrollContent);

    fontColorRow->addWidget(fontColorButton);
    fontColorRow->addWidget(makeItemLabel("Màu chữ"));
    fontColorRow->addStretch();

    row1->addLayout(backgroundColorRow, 1);
    row1->addLayout(fontColorRow, 1);
    layout->addLayout(row1);

    // Cỡ chữ và in đậm.
    auto *row2 = new QHBoxLayout();
    row2->setSpacing(24);

    auto *fontSizeRow = new QHBoxLayout();
    fontSizeRow->setSpacing(12);

    auto *fontSizeStepper =
        new CompactStepper(
            8, 72, m_settings.fontSize, scrollContent);

    fontSizeRow->addWidget(fontSizeStepper);
    fontSizeRow->addWidget(makeItemLabel("Cỡ chữ tối đa (px)"));
    fontSizeRow->addStretch();

    auto *boldRow = new QHBoxLayout();
    boldRow->setSpacing(12);

    auto *boldSwitch = new ToggleSwitch(scrollContent);
    boldSwitch->setChecked(m_settings.isBold);

    boldRow->addWidget(boldSwitch);
    boldRow->addWidget(makeItemLabel("In đậm"));
    boldRow->addStretch();

    row2->addLayout(fontSizeRow, 1);
    row2->addLayout(boldRow, 1);
    layout->addLayout(row2);

    // Khoảng cách dòng và giữ định dạng gốc.
    auto *row3 = new QHBoxLayout();
    row3->setSpacing(24);

    auto *lineSpacingRow = new QHBoxLayout();
    lineSpacingRow->setSpacing(12);

    auto *lineSpacingStepper =
        new CompactStepper(
            6, 60, m_settings.lineSpacing, scrollContent);

    lineSpacingRow->addWidget(lineSpacingStepper);
    lineSpacingRow->addWidget(makeItemLabel("Line spacing"));
    lineSpacingRow->addStretch();

    auto *keepFormatRow = new QHBoxLayout();
    keepFormatRow->setSpacing(12);

    auto *keepFormatSwitch = new ToggleSwitch(scrollContent);
    keepFormatSwitch->setChecked(
        m_settings.keepSourceFormatting);

    keepFormatRow->addWidget(keepFormatSwitch);
    keepFormatRow->addWidget(
        makeItemLabel("Keep source formatting"));
    keepFormatRow->addStretch();

    row3->addLayout(lineSpacingRow, 1);
    row3->addLayout(keepFormatRow, 1);
    layout->addLayout(row3);

    // Căn lề.
    auto *alignmentCombo = new QComboBox(scrollContent);
    alignmentCombo->setFixedWidth(240);
    alignmentCombo->setFixedHeight(36);
    alignmentCombo->addItems({"Align Text Left",
                              "Align Text Center",
                              "Align Text Right"});
    alignmentCombo->setCurrentIndex(m_settings.textAlignment);

    alignmentCombo->setStyleSheet(R"(
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

    layout->addWidget(alignmentCombo);

    // Tự xóa nội dung.
    auto *autoClearRow = new QHBoxLayout();
    autoClearRow->setSpacing(12);

    auto *autoClearSwitch = new ToggleSwitch(scrollContent);
    autoClearSwitch->setChecked(m_settings.autoClearWindow);

    autoClearRow->addWidget(autoClearSwitch);
    autoClearRow->addWidget(makeItemLabel("Auto clear window"));
    autoClearRow->addStretch();

    layout->addLayout(autoClearRow);

    // Loại overlay khỏi ảnh chụp.
    auto *excludeCaptureRow = new QHBoxLayout();
    excludeCaptureRow->setSpacing(12);

    auto *excludeCaptureSwitch = new ToggleSwitch(scrollContent);
    excludeCaptureSwitch->setChecked(
        m_settings.excludeFromCapture);

    excludeCaptureRow->addWidget(excludeCaptureSwitch);
    excludeCaptureRow->addWidget(
        makeItemLabel("Exclude from capture"));
    excludeCaptureRow->addStretch();

    layout->addLayout(excludeCaptureRow);

    // Độ đục của nền.
    auto *opacityLayout = new QVBoxLayout();
    opacityLayout->setSpacing(6);

    auto *opacityLabelRow = new QHBoxLayout();
    opacityLabelRow->addWidget(makeItemLabel("Độ đục của nền"));

    auto *opacityValueLabel =
        new QLabel(
            QString("%1%").arg(m_settings.windowOpacity),
            scrollContent);

    opacityValueLabel->setStyleSheet(
        "color: #38bdf8;"
        "font-size: 10pt;"
        "font-weight: bold;");

    opacityLabelRow->addWidget(opacityValueLabel);
    opacityLabelRow->addStretch();
    opacityLayout->addLayout(opacityLabelRow);

    auto *opacitySlider =
        new QSlider(Qt::Horizontal, scrollContent);

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

    opacityLayout->addWidget(opacitySlider);
    layout->addLayout(opacityLayout);

    // Nút bật/tắt ô xem trước.
    auto *previewButtonRow = new QHBoxLayout();

    auto *previewButton =
        new QPushButton("Show text example", scrollContent);

    previewButton->setFixedHeight(36);
    previewButton->setFixedWidth(170);
    previewButton->setCursor(Qt::PointingHandCursor);

    previewButton->setStyleSheet(R"(
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

    previewButtonRow->addWidget(previewButton);
    previewButtonRow->addStretch();
    layout->addLayout(previewButtonRow);

    // Ô xem trước.
    m_previewFrame = new QFrame(scrollContent);
    m_previewFrame->setObjectName("previewFrame");
    m_previewFrame->setMinimumHeight(80);

    auto *previewLayout = new QVBoxLayout(m_previewFrame);
    previewLayout->setContentsMargins(16, 12, 16, 12);
    previewLayout->setSpacing(6);

    auto *sampleTag =
        new QLabel(
            "VÍ DỤ HIỂN THỊ VĂN BẢN DỊCH",
            m_previewFrame);

    sampleTag->setStyleSheet(
        "color: #64748b;"
        "font-size: 7.5pt;"
        "font-weight: bold;"
        "letter-spacing: 1px;");

    previewLayout->addWidget(sampleTag);

    m_previewTextLabel = new QLabel(
        "Chào mừng bạn đến với EZ-Translator!\n"
        "Welcome to EZ-Translator real-time game subtitle.",
        m_previewFrame);

    m_previewTextLabel->setWordWrap(true);
    previewLayout->addWidget(m_previewTextLabel);

    layout->addWidget(m_previewFrame);
    layout->addStretch();

    scroll->setWidget(scrollContent);
    mainLayout->addWidget(scroll, 1);

    // Các tùy chọn chưa hỗ trợ chỉnh trực tiếp.
    lineSpacingStepper->setEnabled(false);
    keepFormatSwitch->setEnabled(false);
    autoClearSwitch->setEnabled(false);

    excludeCaptureSwitch->setChecked(true);
    excludeCaptureSwitch->setEnabled(false);

    lineSpacingStepper->setToolTip(
        "Chưa hỗ trợ chỉnh khoảng cách dòng.");

    keepFormatSwitch->setToolTip(
        "Chưa hỗ trợ giữ định dạng gốc.");

    autoClearSwitch->setToolTip(
        "Hiện do phiên dịch tự quản lý.");

    excludeCaptureSwitch->setToolTip(
        "Luôn bật để tránh chụp lại overlay.");

    // Xử lý chọn màu nền.
    connect(
        backgroundColorButton,
        &QPushButton::clicked,
        this,
        [this, backgroundColorButton]()
        {
            QColor chosen = chooseOverlayColor(
                m_settings.windowColor,
                this,
                "Chọn màu nền overlay");

            if (chosen.isValid())
            {
                m_settings.windowColor = chosen;
                backgroundColorButton->setColor(chosen);
                updatePreview();
            }
        });

    // Xử lý chọn màu chữ.
    connect(
        fontColorButton,
        &QPushButton::clicked,
        this,
        [this, fontColorButton]()
        {
            QColor chosen = chooseOverlayColor(
                m_settings.fontColor,
                this,
                "Chọn màu chữ dịch");

            if (chosen.isValid())
            {
                m_settings.fontColor = chosen;
                fontColorButton->setColor(chosen);
                updatePreview();
            }
        });

    fontSizeStepper->setOnChanged([this](int value)
                                  {
        m_settings.fontSize = value;
        updatePreview(); });

    connect(
        boldSwitch,
        &QAbstractButton::toggled,
        this,
        [this](bool checked)
        {
            m_settings.isBold = checked;
            updatePreview();
        });

    lineSpacingStepper->setOnChanged([this](int value)
                                     {
        m_settings.lineSpacing = value;
        updatePreview(); });

    connect(
        keepFormatSwitch,
        &QAbstractButton::toggled,
        this,
        [this](bool checked)
        {
            m_settings.keepSourceFormatting = checked;
        });

    connect(
        alignmentCombo,
        QOverload<int>::of(&QComboBox::currentIndexChanged),
        this,
        [this](int index)
        {
            m_settings.textAlignment = index;
            updatePreview();
        });

    connect(
        autoClearSwitch,
        &QAbstractButton::toggled,
        this,
        [this](bool checked)
        {
            m_settings.autoClearWindow = checked;
        });

    connect(
        excludeCaptureSwitch,
        &QAbstractButton::toggled,
        this,
        [this](bool checked)
        {
            m_settings.excludeFromCapture = checked;
        });

    connect(
        opacitySlider,
        &QSlider::valueChanged,
        this,
        [this, opacityValueLabel](int value)
        {
            m_settings.windowOpacity = value;
            opacityValueLabel->setText(
                QString("%1%").arg(value));
            updatePreview();
        });

    connect(
        previewButton,
        &QPushButton::clicked,
        this,
        [this]()
        {
            m_previewFrame->setVisible(
                !m_previewFrame->isVisible());
        });

    updatePreview();
    return page;
}

QWidget *SettingsDialog::buildHotkeysPage()
{
    auto *page = makePage();
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(16);

    layout->addWidget(makePageTitle("Phím tắt"));
    layout->addWidget(makeDivider());
    layout->addSpacing(6);

    struct HotkeyRow
    {
        QString label;
        QString defaultKey;
    };

    const QList<HotkeyRow> rows = {
        {"Bắt đầu / Dừng dịch", "F8"},
        {"Chọn vùng dịch", "F6"},
        {"Xem / Ẩn khung vùng dịch", "F7"},
        {"Mở cửa sổ Cài đặt", "Ctrl + ,"}};

    for (const auto &row : rows)
    {
        auto *rowLayout = new QHBoxLayout();
        rowLayout->setSpacing(16);

        auto *keyEdit = new QComboBox(page);
        keyEdit->setFixedWidth(180);
        keyEdit->setFixedHeight(36);
        keyEdit->addItems({row.defaultKey,
                           "(Nhấn để gán phím mới)"});

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
            QComboBox:hover {
                border-color: #38bdf8;
            }
            QComboBox::drop-down {
                border: none;
                width: 24px;
            }
        )");

        auto *label = new QLabel(row.label, page);
        label->setStyleSheet(
            "color: #e2e8f0;"
            "font-size: 10.5pt;"
            "font-weight: 500;");

        rowLayout->addWidget(keyEdit);
        rowLayout->addWidget(label);
        rowLayout->addStretch();

        layout->addLayout(rowLayout);
        layout->addSpacing(2);
    }

    layout->addStretch();
    return page;
}

void SettingsDialog::setSettings(
    const EZTranslator::AppSettings &settings)
{
    m_settings = settings;

    // Tạo lại trang từ cài đặt đã áp dụng.
    // Các thay đổi từng bị Hủy sẽ được bỏ.
    const int currentPage = m_stack->currentIndex();

    QWidget *oldPage = m_stack->widget(1);
    m_stack->removeWidget(oldPage);

    m_previewFrame = nullptr;
    m_previewTextLabel = nullptr;

    delete oldPage;

    m_stack->insertWidget(1, buildOverlayPage());
    m_stack->setCurrentIndex(currentPage);
}

EZTranslator::AppSettings SettingsDialog::loadSettings()
{
    EZTranslator::AppSettings result;

    QSettings store(
        QSettings::IniFormat,
        QSettings::UserScope,
        "EZTranslator",
        "EZTranslator");

    store.setFallbacksEnabled(false);
    store.beginGroup("overlay");

    const auto readInt = [&](const char *key,
                             int fallback,
                             int low,
                             int high)
    {
        bool ok = false;
        const int value =
            store.value(key, fallback).toInt(&ok);

        return ok ? std::clamp(value, low, high) : fallback;
    };

    const auto readColor = [&](const char *key,
                               const QColor &fallback)
    {
        QColor color(
            store.value(key, fallback.name()).toString());

        if (!color.isValid())
            color = fallback;

        color.setAlpha(255);
        return color;
    };

    result.windowColor =
        readColor("background", result.windowColor);

    result.fontColor =
        readColor("textColor", result.fontColor);

    result.fontSize =
        readInt("fontSize", result.fontSize, 8, 72);

    result.windowOpacity =
        readInt("opacity", result.windowOpacity, 10, 100);

    result.textAlignment =
        readInt("alignment", result.textAlignment, 0, 2);

    result.isBold =
        store.value("bold", result.isBold).toBool();

    return result;
}

bool SettingsDialog::saveSettings(
    const EZTranslator::AppSettings &settings)
{
    QSettings store(
        QSettings::IniFormat,
        QSettings::UserScope,
        "EZTranslator",
        "EZTranslator");

    store.setFallbacksEnabled(false);
    store.beginGroup("overlay");

    store.setValue(
        "background",
        settings.windowColor.name());

    store.setValue(
        "textColor",
        settings.fontColor.name());

    store.setValue(
        "fontSize",
        std::clamp(settings.fontSize, 8, 72));

    store.setValue(
        "opacity",
        std::clamp(settings.windowOpacity, 10, 100));

    store.setValue(
        "alignment",
        std::clamp(settings.textAlignment, 0, 2));

    store.setValue("bold", settings.isBold);

    store.endGroup();
    store.sync();

    return store.status() == QSettings::NoError;
}