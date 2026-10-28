#include <QtTest/QtTest>

#include "overlay/TranslationPopup.h"

#include <QGuiApplication>
#include <QLabel>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>

class TestTranslationPopup : public QObject
{
    Q_OBJECT

private slots:
    void longSelectionRemainsScrollable()
    {
        EZTranslator::TranslationPopup popup;
        QStringList parts;
        for (int i = 0; i < 70; ++i)
            parts.append(QStringLiteral("Đây là một câu dịch dài để kiểm tra khả năng cuộn."));
        const QString fullText = parts.join(QLatin1Char(' '));
        const QRect screen = QGuiApplication::primaryScreen()->availableGeometry();
        const QRect selection(screen.center(), QSize(40, 20));
        popup.showTranslation(QString(), fullText, selection.center(), 460, selection);

        QVERIFY(popup.isVisible());
        auto* area = popup.findChild<QScrollArea*>();
        QVERIFY(area != nullptr);
        QVERIFY(area->verticalScrollBar()->maximum() > 0);
        QVERIFY(popup.height() <= screen.height());
        bool hasFullText = false;
        for (const QLabel* label : popup.findChildren<QLabel*>())
            hasFullText |= label->text() == fullText;
        QVERIFY(hasFullText);
    }
};

QTEST_MAIN(TestTranslationPopup)
#include "test_translation_popup.moc"
