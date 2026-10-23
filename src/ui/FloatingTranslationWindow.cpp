#include "ui/FloatingTranslationWindow.h"

#include <QCloseEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QVBoxLayout>

FloatingTranslationWindow::FloatingTranslationWindow(QWidget* parent)
    : QDialog(parent, Qt::Window | Qt::WindowStaysOnTopHint)
{
    setObjectName(QStringLiteral("floatingTranslationWindow"));
    setWindowTitle(QStringLiteral("EZ Translator - Cửa sổ nổi"));
    setWindowModality(Qt::NonModal);
    setAttribute(Qt::WA_TranslucentBackground, true);
    resize(720, 560);
    setMinimumSize(420, 320);

    setStyleSheet(QStringLiteral(R"(
        QDialog#floatingTranslationWindow {
            background: #0f172a;
            color: #e2e8f0;
        }
        QLabel#sectionLabel {
            color: #7dd3fc;
            font-size: 9pt;
            font-weight: 700;
            letter-spacing: 1px;
        }
        QLabel#hintLabel {
            color: #64748b;
            font-size: 8.5pt;
        }
        QPlainTextEdit {
            background: #111c2e;
            color: #f8fafc;
            border: 1px solid #26364d;
            border-radius: 8px;
            padding: 8px;
            selection-background-color: #1d4ed8;
            font-size: 11pt;
        }
        QPlainTextEdit#translatedText {
            background: #0b1320;
            border-color: #2f4a6b;
            font-size: 12.5pt;
        }
    )"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 14, 16, 14);
    root->setSpacing(8);

    auto* sourceLabel = new QLabel(QStringLiteral("VĂN BẢN OCR"), this);
    sourceLabel->setObjectName(QStringLiteral("sectionLabel"));
    root->addWidget(sourceLabel);
    m_sourceLabel = sourceLabel;

    m_sourceText = new QPlainTextEdit(this);
    m_sourceText->setReadOnly(true);
    m_sourceText->setPlaceholderText(QStringLiteral("Đang chờ văn bản OCR..."));
    m_sourceText->setMaximumHeight(190);
    root->addWidget(m_sourceText, 1);

    auto* translatedLabel = new QLabel(QStringLiteral("BẢN DỊCH"), this);
    translatedLabel->setObjectName(QStringLiteral("sectionLabel"));
    root->addWidget(translatedLabel);
    m_translatedLabel = translatedLabel;

    m_translatedText = new QPlainTextEdit(this);
    m_translatedText->setObjectName(QStringLiteral("translatedText"));
    m_translatedText->setReadOnly(true);
    m_translatedText->setPlaceholderText(QStringLiteral("Đang dịch..."));
    root->addWidget(m_translatedText, 2);

    auto* hint = new QLabel(
        QStringLiteral("Có thể kéo cửa sổ này sang vị trí khác. Đóng cửa sổ để dừng dịch."),
        this);
    hint->setObjectName(QStringLiteral("hintLabel"));
    root->addWidget(hint);

    applyAppearance();
}

void FloatingTranslationWindow::setAppearance(const EZTranslator::AppSettings& settings)
{
    m_appearance = settings;
    applyAppearance();
}

void FloatingTranslationWindow::applyAppearance()
{
    if (!m_sourceText || !m_translatedText)
        return;

    const int fontSize = qBound(8, m_appearance.fontSize, 48);
    QFont translatedFont(m_appearance.fontFamily, fontSize);
    translatedFont.setBold(m_appearance.isBold);
    QFont sourceFont(m_appearance.fontFamily, qMax(8, fontSize - 3));
    m_translatedText->setFont(translatedFont);
    m_sourceText->setFont(sourceFont);

    const int style = qBound(0, m_appearance.textWindowStyle, 3);
    QColor textColor = m_appearance.fontColor.isValid() ? m_appearance.fontColor
                                                        : QColor(248, 250, 252);
    if (style == 3)
        textColor = QColor(26, 26, 26);
    QColor sourceColor = textColor;
    sourceColor.setAlpha(170);

    QColor background = m_appearance.windowColor.isValid()
                            ? m_appearance.windowColor
                            : QColor(15, 23, 42);
    if (style == 2) {
        background.setAlpha(225);
    } else if (style == 3) {
        background = QColor(248, 246, 240);
        background.setAlpha(255);
    } else if (style == 1) {
        const int opacity = qBound(10, m_appearance.windowOpacity, 100);
        background.setAlpha(qBound(70, qRound(255.0 * opacity / 100.0 * 0.72), 220));
    } else {
        background.setAlpha(255);
    }

    const bool outline = style == 2;
    const bool glass = style == 1;
    const QString border =
        outline || glass
            ? QStringLiteral("1px solid rgba(130,150,180,%1)").arg(outline ? 220 : 150)
            : QStringLiteral("1px solid transparent");
    const int editAlpha = outline ? 40 : (glass ? 160 : 245);
    m_sourceText->setObjectName(QStringLiteral("sourceText"));

    const QString sheet =
        QStringLiteral(R"(
        QDialog#floatingTranslationWindow {
            background-color: rgba(%1,%2,%3,%4);
            border: %5;
            border-radius: 12px;
        }
        QLabel#sectionLabel {
            color: rgba(%6,%7,%8,220);
            font-family: "%9";
            font-size: %10pt;
            font-weight: 700;
        }
        QLabel#hintLabel {
            color: rgba(%6,%7,%8,140);
            font-family: "%9";
            font-size: %11pt;
        }
        QPlainTextEdit {
            background-color: rgba(%12,%13,%14,%15);
            color: rgba(%6,%7,%8,%16);
            border: %5;
            border-radius: 8px;
            padding: 8px;
            selection-background-color: #1d4ed8;
            font-family: "%9";
        }
        QPlainTextEdit#sourceText {
            font-size: %17pt;
        }
        QPlainTextEdit#translatedText {
            font-size: %18pt;
        }
        )")
            .arg(background.red())
            .arg(background.green())
            .arg(background.blue())
            .arg(background.alpha())
            .arg(border)
            .arg(textColor.red())
            .arg(textColor.green())
            .arg(textColor.blue())
            .arg(m_appearance.fontFamily)
            .arg(qMax(8, fontSize - 6))
            .arg(qMax(8, fontSize - 8))
            .arg(qMax(0, background.red() - 8))
            .arg(qMax(0, background.green() - 8))
            .arg(qMax(0, background.blue() - 8))
            .arg(editAlpha)
            .arg(textColor.alpha())
            .arg(qMax(8, fontSize - 3))
            .arg(fontSize);
    setStyleSheet(sheet);

    const bool showSource = m_appearance.textWindowShowSource;
    if (m_sourceLabel)
        m_sourceLabel->setVisible(showSource);
    m_sourceText->setVisible(showSource);
}

void FloatingTranslationWindow::setSourceText(const QString& text)
{
    if (!m_sourceText)
        return;
    const QString value = text.trimmed();
    if (m_sourceText->toPlainText() == value)
        return;
    m_sourceText->setPlainText(value);
}

void FloatingTranslationWindow::setTranslatedText(const QString& text)
{
    if (!m_translatedText)
        return;
    const QString value = text.trimmed();
    if (m_translatedText->toPlainText() == value)
        return;
    m_translatedText->setPlainText(value);
}

void FloatingTranslationWindow::setStatusText(const QString& text)
{
    if (m_translatedText && m_translatedText->toPlainText().trimmed().isEmpty())
        m_translatedText->setPlaceholderText(text);
}

void FloatingTranslationWindow::closeEvent(QCloseEvent* event)
{
    emit closedByUser();
    QDialog::closeEvent(event);
}
