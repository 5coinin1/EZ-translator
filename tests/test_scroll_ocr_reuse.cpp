#include <QtTest/QtTest>

#include <cstring>
#include <cmath>

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
    void bootstrapDoesNotPreferZeroAlias()
    {
        GrayProfile reference;
        GrayProfile current;
        for (int y = 0; y < 500; ++y) {
            reference.rows.append(100.0 + 40.0 * std::cos(y * 0.157079632679)
                                  + ((y * 37) % 23) * 0.1);
        }
        for (int y = 0; y < 500; ++y)
            current.rows.append(reference.rows[(y + 40) % 500]);
        reference.cols = QList<double>(320, 100.0);
        current.cols = reference.cols;

        const auto initial = matchProfile(reference, current, 100, 0.92, 0.0, false);
        QVERIFY(initial.valid);
        QCOMPARE(initial.dy, -40);
    }

    void edgesCanRecoverScrollWhenLuminanceReportsZero()
    {
        GrayProfile reference;
        GrayProfile current;
        quint32 noise = 12345;
        for (int y = 0; y < 500; ++y) {
            noise = noise * 1664525u + 1013904223u;
            reference.rows.append(100.0 + double(noise >> 24));
            current.rows.append(reference.rows.last() + ((y * 11) % 7));
            reference.edgeRows.append(double((y * y * 13 + y * 37) % 251));
        }
        for (int y = 0; y < 500; ++y)
            current.edgeRows.append(reference.edgeRows[(y + 40) % 500]);
        reference.cols = QList<double>(320, 100.0);
        current.cols = reference.cols;

        const auto offset = matchProfile(reference, current, 100, 0.92, 0.0, false);
        QVERIFY(offset.valid);
        QVERIFY(offset.usedGradient);
        QCOMPARE(offset.dy, -40);
    }

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

    void emptyBandReusesVisibleText()
    {
        const QImage first = page(3);
        QImage scrolled = page(97);
        for (int y = 0; y < 460; ++y)
            std::memcpy(scrolled.scanLine(y), first.constScanLine(y + 40),
                        size_t(first.width()));

        ScrollOcrReuse reuse;
        reuse.accept(first, first.rect(), {{QRect(20, 150, 90, 18), "old", 1.0f}});
        const auto firstStep = reuse.tryIncremental(scrolled, scrolled.rect(),
            [](const QImage&) { return QList<OcrTextBox>{}; });
        QVERIFY(firstStep.ok);
        QCOMPARE(firstStep.boxes.size(), 1);
        QCOMPARE(firstStep.boxes.first().rect.y(), 110);

        QCOMPARE(reuse.boxes().size(), 1);
        QCOMPARE(reuse.boxes().first().rect.y(), 110);
    }

    void emptyBandWithNoVisibleTextFallsBack()
    {
        const QImage first = page(3);
        QImage scrolled = page(97);
        for (int y = 0; y < 460; ++y)
            std::memcpy(scrolled.scanLine(y), first.constScanLine(y + 40),
                        size_t(first.width()));

        ScrollOcrReuse reuse;
        reuse.accept(first, first.rect(), {{QRect(20, 4, 90, 18), "old", 1.0f}});
        const auto result = reuse.tryIncremental(scrolled, scrolled.rect(),
            [](const QImage&) { return QList<OcrTextBox>{}; });
        QVERIFY(!result.ok);
        QCOMPARE(reuse.boxes().first().rect.y(), 4);
    }

    void unchangedFrameDoesNotRunBandOcr()
    {
        const QImage frame = page(3);
        ScrollOcrReuse reuse;
        reuse.accept(frame, frame.rect(), {{QRect(20, 150, 90, 18), "old", 1.0f}});

        int calls = 0;
        const auto result = reuse.tryIncremental(frame, frame.rect(),
            [&calls](const QImage&) {
                ++calls;
                return QList<OcrTextBox>{};
            });

        QVERIFY(!result.ok);
        QCOMPARE(QString::fromLatin1(result.reason), QStringLiteral("no-vertical-shift"));
        QCOMPARE(calls, 0);
    }

    void relaxedScoreVerticalScrollIsReused()
    {
        const QImage first = page(3);
        QImage scrolled = page(97);
        for (int y = 0; y < 460; ++y)
            std::memcpy(scrolled.scanLine(y), first.constScanLine(y + 40),
                        size_t(first.width()));

        QImage relaxedFrame;
        ScrollOffset relaxedOffset;
        const GrayProfile reference = grayProfile(first, first.rect(), 8, true);
        for (int amplitude = 2; amplitude <= 160; amplitude += 2) {
            QImage candidate = scrolled;
            for (int y = 0; y < 460; ++y) {
                uchar *row = candidate.scanLine(y);
                const int delta = ((y * 53) % (2 * amplitude + 1)) - amplitude;
                for (int x = 0; x < candidate.width(); ++x)
                    row[x] = uchar(qBound(0, int(row[x]) + delta, 255));
            }

            const GrayProfile current = grayProfile(candidate, candidate.rect(), 8, true);
            const ScrollOffset offset = matchProfile(reference, current,
                                                      first.height() / 2, 0.92, 0.0);
            if (offset.valid && offset.score < 0.96 && offset.dy == -40) {
                relaxedFrame = candidate;
                relaxedOffset = offset;
                break;
            }
        }

        QVERIFY2(!relaxedFrame.isNull(), "Could not create a deterministic relaxed-score frame");
        QVERIFY(relaxedOffset.score >= 0.92);
        QVERIFY(relaxedOffset.score < 0.96);

        ScrollOcrReuse reuse;
        reuse.accept(first, first.rect(), {{QRect(20, 150, 90, 18), "old", 1.0f}});
        int calls = 0;
        const auto result = reuse.tryIncremental(relaxedFrame, relaxedFrame.rect(),
            [&calls](const QImage&) {
                ++calls;
                return QList<OcrTextBox>{};
            });

        QVERIFY(result.ok);
        QCOMPARE(result.dy, -40);
        QCOMPARE(calls, 1);
    }
};

QTEST_GUILESS_MAIN(TestScrollOcrReuse)
#include "test_scroll_ocr_reuse.moc"
