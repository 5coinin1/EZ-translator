#include <QtTest/QtTest>

#include <cstring>

#include "scroll/ScrollOcrReuse.h"

class TestScrollOcrReuse : public QObject
{
    Q_OBJECT

private:
    static QImage page(int seed)
    {
        QImage image(320, 500, QImage::Format_Grayscale8);
        for (int y = 0; y < image.height(); ++y) {
            uchar* row = image.scanLine(y);
            for (int x = 0; x < image.width(); ++x)
                row[x] = uchar((x * 17 + y * 37 + ((y * y + seed) % 71) * 13) % 256);
        }
        return image;
    }

private slots:
    void reusesScrollAndRecognizesOnlyBand()
    {
        const QImage first = page(3);
        QImage scrolled = page(97);
        for (int y = 0; y < 460; ++y)
            std::memcpy(scrolled.scanLine(y), first.constScanLine(y + 40),
                        size_t(first.width()));

        ScrollOcrReuse reuse;
        reuse.accept(first, first.rect(), {{QRect(20, 150, 90, 18), "old", 1.0f}});
        int calls = 0;
        const auto result = reuse.tryIncremental(scrolled, scrolled.rect(),
            [&calls](const QImage& band) {
                ++calls;
                if (band.height() >= 150)
                    return QList<OcrTextBox>{};
                return QList<OcrTextBox>{{QRect(20, band.height() - 24, 90, 18),
                                          "new", 1.0f}};
            });
        QVERIFY(result.ok);
        QCOMPARE(calls, 1);
        QCOMPARE(result.dy, -40);
        QCOMPARE(result.boxes.size(), 2);
        QCOMPARE(result.boxes[0].rect.y(), 110);
        QCOMPARE(result.boxes[1].text, QStringLiteral("new"));
    }

    void changedPageFallsBackWithoutBandOcr()
    {
        const QImage first = page(3);
        const QImage changed = page(999);
        ScrollOcrReuse reuse;
        reuse.accept(first, first.rect(), {{QRect(20, 150, 90, 18), "old", 1.0f}});
        int calls = 0;
        const auto result = reuse.tryIncremental(changed, changed.rect(),
            [&calls](const QImage&) {
                ++calls;
                return QList<OcrTextBox>{};
            });
        QVERIFY(!result.ok);
        QCOMPARE(calls, 0);
    }

    void emptyBandDoesNotOverwriteReference()
    {
        const QImage first = page(3);
        QImage scrolled = page(97);
        for (int y = 0; y < 460; ++y)
            std::memcpy(scrolled.scanLine(y), first.constScanLine(y + 40),
                        size_t(first.width()));

        ScrollOcrReuse reuse;
        reuse.accept(first, first.rect(), {{QRect(20, 150, 90, 18), "old", 1.0f}});
        const auto failed = reuse.tryIncremental(scrolled, scrolled.rect(),
            [](const QImage&) { return QList<OcrTextBox>{}; });
        QVERIFY(!failed.ok);
        QCOMPARE(reuse.boxes().first().rect.y(), 150);

        const auto retry = reuse.tryIncremental(scrolled, scrolled.rect(),
            [](const QImage& band) {
                return QList<OcrTextBox>{{QRect(20, band.height() - 24, 90, 18),
                                          "new", 1.0f}};
            });
        QVERIFY(retry.ok);
        QCOMPARE(retry.dy, -40);
    }
};

QTEST_MAIN(TestScrollOcrReuse)
#include "test_scroll_ocr_reuse.moc"
