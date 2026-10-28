#include <QtTest/QtTest>

#include "app/HoverSelectionGeometry.h"

class TestHoverSelection : public QObject
{
    Q_OBJECT

private slots:
    void multiLineDragReadsFullTextColumn()
    {
        const QRect client(0, 0, 2880, 1700);
        const QPoint start(825, 1089);
        const QPoint end(900, 1204);
        const QRect read = EZTranslator::hoverSelectionReadRect(start, end, client);
        QVERIFY(read.left() < 800);
        QVERIFY(read.right() > 1850);
        QVERIFY(read.top() < start.y());
        QVERIFY(read.bottom() > end.y());

        const QRect reversed = EZTranslator::hoverSelectionReadRect(end, start, client);
        QCOMPARE(reversed, read);
    }

    void oneLineDragKeepsTightCropWithGlyphPadding()
    {
        const QRect client(0, 0, 2880, 1700);
        const QRect read = EZTranslator::hoverSelectionReadRect(
            QPoint(825, 1089), QPoint(1050, 1093), client);
        QVERIFY(read.width() < 260);
        QVERIFY(read.height() >= 35);
        QVERIFY(read.contains(QPoint(825, 1089)));
        QVERIFY(read.contains(QPoint(1050, 1093)));
    }
};

QTEST_MAIN(TestHoverSelection)
#include "test_hover_selection.moc"
