/**
 * @file test_detection.cpp
 * @brief Unit tests cho module detection/.
 *
 * Test coverage:
 *   D1 – differenceHash: ảnh null → 0
 *   D2 – hammingDistance: giống nhau → 0, khác → > 0
 *   D3 – ChangeDetector: frame đầu true, frame lặp lại false, frame đổi true
 *   D4 – ChangeDetector: forget()/reset() xóa trạng thái
 *   D5 – HeuristicTextDetector: ảnh trắng trơn → không có box
 *   D6 – HeuristicTextDetector: ảnh có chữ → phát hiện được box trong biên ảnh
 *   D7 – RecursiveXYCut: 2 cụm tách xa → 2 block
 *   D8 – RecursiveXYCut: 1 box → 1 block; danh sách rỗng → rỗng
 */

#include <QtTest/QtTest>

#include <QImage>
#include <QPainter>

#include "detection/ChangeDetector.h"
#include "detection/HeuristicTextDetector.h"
#include "detection/RecursiveXYCut.h"
#include "ocr/OcrScheduler.h"

using namespace EZTranslator;

// ── Helpers ──────────────────────────────────────────────────────────────────

static QImage makeBlankImage(int w, int h)
{
    QImage image(w, h, QImage::Format_RGB32);
    image.fill(Qt::white);
    return image;
}

/** Ảnh trắng có một ô đen ở vị trí cho trước (tạo khác biệt giữa các frame). */
static QImage makeImageWithSquare(int w, int h, const QRect& square)
{
    QImage image = makeBlankImage(w, h);
    QPainter painter(&image);
    painter.fillRect(square, Qt::black);
    return image;
}

/** Ảnh trắng có chữ đen (glyph thật → tỉ lệ lấp đầy < 0.95, qua được filter). */
static QImage makeTextImage(int w, int h)
{
    QImage image = makeBlankImage(w, h);
    QPainter painter(&image);
    painter.setPen(Qt::black);
    QFont font("Arial", 18, QFont::Bold);
    painter.setFont(font);
    painter.drawText(QRect(8, 0, w - 16, h), Qt::AlignVCenter | Qt::AlignLeft,
                     QStringLiteral("TEXT"));
    return image;
}

// ── Test class ────────────────────────────────────────────────────────────────

class TestDetection : public QObject
{
    Q_OBJECT

private slots:

    // ── D1 ───────────────────────────────────────────────────────────────────
    void hash_nullImageIsZero()
    {
        QCOMPARE(ChangeDetector::differenceHash(QImage{}), quint64(0));
    }

    // ── D2 ───────────────────────────────────────────────────────────────────
    void hash_distanceBasics()
    {
        const QImage a = makeImageWithSquare(64, 64, QRect(4, 4, 16, 16));
        const QImage b = makeImageWithSquare(64, 64, QRect(40, 40, 16, 16));

        const quint64 ha = ChangeDetector::differenceHash(a);
        const quint64 hb = ChangeDetector::differenceHash(b);

        QCOMPARE(ChangeDetector::hammingDistance(ha, ha), 0);
        QVERIFY(ChangeDetector::hammingDistance(ha, hb) > 0);
    }

    // ── D3 ───────────────────────────────────────────────────────────────────
    void changeDetector_detectsChanges()
    {
        ChangeDetector detector;
        const QImage a = makeImageWithSquare(64, 64, QRect(4, 4, 16, 16));
        const QImage b = makeImageWithSquare(64, 64, QRect(40, 40, 16, 16));

        QVERIFY(detector.hasChanged("roi", a));   // lần đầu luôn true
        QVERIFY(!detector.hasChanged("roi", a));  // y hệt → không đổi
        QVERIFY(detector.hasChanged("roi", b));   // khác → đổi
    }

    // ── D4 ───────────────────────────────────────────────────────────────────
    void changeDetector_forgetAndReset()
    {
        ChangeDetector detector;
        const QImage a = makeImageWithSquare(64, 64, QRect(4, 4, 16, 16));

        QVERIFY(detector.hasChanged("roi", a));
        QVERIFY(!detector.hasChanged("roi", a));

        detector.forget("roi");
        QVERIFY(detector.hasChanged("roi", a)); // đã quên → coi như mới

        detector.reset();
        QVERIFY(detector.hasChanged("roi", a));
    }

    // ── D5 ───────────────────────────────────────────────────────────────────
    void heuristic_blankImageHasNoBoxes()
    {
        HeuristicTextDetector detector;
        const QList<TextBox> boxes = detector.detect(makeBlankImage(200, 80));
        QCOMPARE(boxes.size(), 0);
    }

    // ── D6 ───────────────────────────────────────────────────────────────────
    void heuristic_detectsText()
    {
        HeuristicTextDetector detector;
        const QImage image = makeTextImage(320, 120);
        const QList<TextBox> boxes = detector.detect(image);

        QVERIFY(!boxes.isEmpty());
        for (const TextBox& box : boxes) {
            QVERIFY(box.rect.width() > 0);
            QVERIFY(box.rect.height() > 0);
            QVERIFY(box.rect.left() >= 0);
            QVERIFY(box.rect.top() >= 0);
            QVERIFY(box.rect.right() <= image.width());
            QVERIFY(box.rect.bottom() <= image.height());
            QVERIFY(box.confidence > 0.0f);
        }
    }

    // ── D7 ───────────────────────────────────────────────────────────────────
    void xyCut_twoColumns()
    {
        const QList<QRect> rects = { QRect(0, 0, 10, 10), QRect(100, 0, 10, 10) };
        const QList<QRect> blocks = RecursiveXYCut::cut(rects);
        QCOMPARE(blocks.size(), 2);
    }

    // ── D8 ───────────────────────────────────────────────────────────────────
    void xyCut_singleAndEmpty()
    {
        QCOMPARE(RecursiveXYCut::cut({ QRect(0, 0, 10, 10) }).size(), 1);
        QCOMPARE(RecursiveXYCut::cut({}).size(), 0);
    }

    // ── D9: scheduler defer frame đầu, chạy sau settle, tôn trọng cooldown ────
    void scheduler_settleAndCooldown()
    {
        OcrScheduler::Options options;
        options.settleMs = 100;
        options.minRunIntervalMs = 500;
        options.maxRunsPerTick = 1;
        OcrScheduler scheduler(options);

        const QImage a = makeImageWithSquare(64, 64, QRect(4, 4, 16, 16));

        scheduler.beginTick();
        OcrScheduleDecision decision = scheduler.process("r", a, 0);
        QVERIFY(!decision.run);
        QVERIFY(decision.changed); // lần đầu → chờ settle

        scheduler.beginTick();
        decision = scheduler.process("r", a, 150); // đã settle
        QVERIFY(decision.run);

        scheduler.beginTick();
        decision = scheduler.process("r", a, 200); // trong cooldown
        QVERIFY(!decision.run);
        QVERIFY(decision.deferred);

        scheduler.beginTick();
        decision = scheduler.process("r", a, 700); // qua cooldown
        QVERIFY(decision.run);
    }

    // ── D10: đổi nội dung reset settle ───────────────────────────────────────
    void scheduler_changeResetsSettle()
    {
        OcrScheduler scheduler; // settle mặc định
        const QImage a = makeImageWithSquare(64, 64, QRect(4, 4, 16, 16));
        const QImage b = makeImageWithSquare(64, 64, QRect(40, 40, 16, 16));

        scheduler.beginTick();
        scheduler.process("r", a, 0);

        scheduler.beginTick();
        OcrScheduleDecision decision = scheduler.process("r", b, 1000);
        QVERIFY(decision.changed);
        QVERIFY(!decision.run);

        scheduler.beginTick();
        QVERIFY(!scheduler.process("r", b, 1050).run); // < settle

        scheduler.beginTick();
        QVERIFY(scheduler.process("r", b, 1200).run);
    }

    // ── D11: cap số lần chạy mỗi tick ────────────────────────────────────────
    void scheduler_capPerTick()
    {
        OcrScheduler::Options options;
        options.settleMs = 0;
        options.minRunIntervalMs = 0;
        options.maxRunsPerTick = 1;
        OcrScheduler scheduler(options);

        const QImage a = makeImageWithSquare(64, 64, QRect(4, 4, 16, 16));

        scheduler.beginTick();
        scheduler.process("r1", a, 0); // lần đầu → deferred
        scheduler.beginTick();
        scheduler.process("r2", a, 0); // lần đầu → deferred

        scheduler.beginTick();
        QVERIFY(scheduler.process("r1", a, 100).run);
        QVERIFY(!scheduler.process("r2", a, 100).run); // đã đạt cap

        scheduler.beginTick();
        QVERIFY(scheduler.process("r2", a, 100).run); // tick mới
    }

    // ── D12: reset xóa trạng thái ────────────────────────────────────────────
    void scheduler_reset()
    {
        OcrScheduler::Options options;
        options.settleMs = 0;
        options.minRunIntervalMs = 0;
        OcrScheduler scheduler(options);
        const QImage a = makeImageWithSquare(64, 64, QRect(4, 4, 16, 16));

        scheduler.beginTick();
        scheduler.process("r", a, 0);
        scheduler.beginTick();
        QVERIFY(scheduler.process("r", a, 100).run);

        scheduler.reset();
        scheduler.beginTick();
        QVERIFY(!scheduler.process("r", a, 200).run); // như mới gặp lại
    }
};

QTEST_MAIN(TestDetection)
#include "test_detection.moc"
