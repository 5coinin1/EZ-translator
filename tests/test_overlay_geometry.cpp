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
};

QTEST_GUILESS_MAIN(TestOverlayGeometry)
#include "test_overlay_geometry.moc"
