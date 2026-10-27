#include <QtTest/QtTest>

#include "overlay/OverlayLayout.h"
#include "overlay/TextOverlay.h"

class TestOverlayGeometry : public QObject
{
    Q_OBJECT

private slots:
    void scrollMovesTextAndDiscardsOldFrameImages()
    {
        EZTranslator::OverlayBox box;
        box.rect = QRect(10, 35, 100, 24);
        box.coverRects = {QRect(10, 36, 90, 20)};
        box.text = QStringLiteral("Ban dich");
        box.plate = QImage(100, 100, QImage::Format_RGB32);
        box.plateRect = QRect(0, 0, 100, 100);
        box.backdrop = box.plate;
        box.backdropRect = box.plateRect;

        const auto shifted = EZTranslator::shiftOverlayBoxes({box}, -20, QRect(0, 0, 120, 80));
        QCOMPARE(shifted.size(), 1);
        QCOMPARE(shifted.first().rect, QRect(10, 15, 100, 24));
        QCOMPARE(shifted.first().coverRects.first(), QRect(10, 16, 90, 20));
        QCOMPARE(shifted.first().text, box.text);
        QVERIFY(shifted.first().plate.isNull());
        QVERIFY(shifted.first().backdrop.isNull());
        const auto offscreen = EZTranslator::shiftOverlayBoxes(shifted, -60,
                                                               QRect(0, 0, 120, 80));
        QCOMPARE(offscreen.size(), 1);
        QCOMPARE(EZTranslator::shiftOverlayBoxes(offscreen, 60, QRect(0, 0, 120, 80))
                     .first().rect, shifted.first().rect);
        QVERIFY(EZTranslator::shiftOverlayBoxes(offscreen, -100,
                                                QRect(0, 0, 120, 80)).isEmpty());
    }

    void paragraphSlotStopsNearSource()
    {
        EZTranslator::TranslatedDocument doc;
        doc.sourceParagraphs = {QStringLiteral("First line"), QStringLiteral("Second line")};
        doc.paragraphs = {QStringLiteral("Dong dau"), QStringLiteral("Dong hai")};
        doc.lineCounts = {1, 1};
        const QList<QRect> lines = {QRect(20, 20, 90, 20), QRect(20, 70, 90, 20)};
        const auto boxes = EZTranslator::OverlayLayout::buildParagraphBoxes(
            doc, lines, doc.sourceParagraphs, QRect(0, 0, 200, 400));
        QCOMPARE(boxes.size(), 2);
        QVERIFY(boxes.first().rect.bottom() < lines.last().top());
        QVERIFY(boxes.last().rect.bottom() < 120);
        QCOMPARE(boxes.first().fontPixelSize, boxes.last().fontPixelSize);
    }

    void partialNovelTranslationDoesNotCoverUntranslatedText()
    {
        EZTranslator::TranslatedDocument doc;
        doc.paragraphs = {QString(), QStringLiteral("Da dich")};
        EZTranslator::OverlayLayout::NovelParagraph first;
        first.rect = QRect(20, 20, 200, 20);
        first.lineRects = {first.rect};
        first.lineHeight = 20;
        EZTranslator::OverlayLayout::NovelParagraph second = first;
        second.rect.moveTop(70);
        second.lineRects = {second.rect};

        const auto boxes = EZTranslator::OverlayLayout::buildNovelBoxes(
            doc, {first, second}, QRect(0, 0, 300, 300));
        QCOMPARE(boxes.size(), 1);
        QCOMPARE(boxes.first().text, QStringLiteral("Da dich"));
        QCOMPARE(boxes.first().rect.top(), 70);
        QVERIFY(!boxes.first().coverOnly);
    }

    void readingBlocksDoNotMergeHeadingsCodeAndPageNumbers()
    {
        const QList<QRect> lines = {QRect(20, 10, 210, 18), QRect(20, 33, 220, 18),
                                    QRect(20, 57, 190, 27), QRect(30, 90, 200, 18),
                                    QRect(20, 114, 15, 18)};
        const QStringList text = {QStringLiteral("A long prose line that continues"),
                                  QStringLiteral("on the next line."),
                                  QStringLiteral("Informative Comments"),
                                  QStringLiteral("// Returns an instance"),
                                  QStringLiteral("56")};
        const auto blocks = EZTranslator::OverlayLayout::groupNovelParagraphs(lines, text);
        QCOMPARE(blocks.size(), 4);
        QCOMPARE(blocks.at(0).lineRects.size(), 2);
        QCOMPARE(blocks.at(1).text, QStringLiteral("Informative Comments"));
        QCOMPARE(blocks.at(2).text, QStringLiteral("// Returns an instance"));
        QCOMPARE(blocks.at(3).text, QStringLiteral("56"));
    }

    void twoColumnsKeepTheirOwnTextAndBounds()
    {
        const QList<QRect> lines = {QRect(30, 20, 280, 18), QRect(600, 20, 280, 18),
                                    QRect(30, 42, 280, 18), QRect(600, 42, 280, 18)};
        const QStringList text = {QStringLiteral("Left column first sentence continues"),
                                  QStringLiteral("Right column first sentence continues"),
                                  QStringLiteral("left column second line ends here"),
                                  QStringLiteral("right column second line ends here")};
        const auto blocks = EZTranslator::OverlayLayout::groupNovelParagraphs(lines, text);
        QCOMPARE(blocks.size(), 2);
        QCOMPARE(blocks.at(0).lineRects.size(), 2);
        QCOMPARE(blocks.at(1).lineRects.size(), 2);
        QVERIFY(blocks.at(0).text.contains(QStringLiteral("Left column")));
        QVERIFY(!blocks.at(0).text.contains(QStringLiteral("Right column")));

        EZTranslator::TranslatedDocument doc;
        doc.paragraphs = {QStringLiteral("Cot trai"), QStringLiteral("Cot phai")};
        const auto boxes = EZTranslator::OverlayLayout::buildNovelBoxes(
            doc, blocks, QRect(0, 0, 1000, 500));
        QCOMPARE(boxes.size(), 2);
        QVERIFY(boxes.at(0).rect.right() < boxes.at(1).rect.left());
    }
};

QTEST_GUILESS_MAIN(TestOverlayGeometry)
#include "test_overlay_geometry.moc"
