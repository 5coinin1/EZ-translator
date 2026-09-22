/**
 * @file test_region_manager.cpp
 * @brief Unit tests cho RegionManager.
 *
 * Test coverage:
 *   T1  – Normalized → pixel đúng resolution 1920×1080
 *   T2  – Normalized → pixel scale đúng resolution 1280×720
 *   T3  – Region chạm cạnh phải/bottom: không out-of-bounds
 *   T4  – Invalid region (width = 0): không extract
 *   T5  – Multiple regions: trả đúng số RegionFrame
 *   T6  – Disabled region: không extract
 *   T7  – Region hơi vượt boundary do float: clamp hợp lý
 *   T8  – Empty/null CapturedFrame: trả empty, không crash
 *
 * Chạy:
 *   Compile riêng với Qt Test (xem CMakeLists.txt target test_region_manager)
 *   hoặc chạy: ninja test_region_manager && ./test_region_manager
 */

#include <QtTest/QtTest>

// Include headers
#include "region/RegionManager.h"
#include "capture/IWindowCapture.h"
#include "core/Types.h"

using namespace EZTranslator;

// ── Helper ───────────────────────────────────────────────────────────────────

/** Tạo một CapturedFrame với ảnh màu xanh kích thước w×h */
static CapturedFrame makeFrame(int w, int h)
{
    CapturedFrame frame;
    if (w > 0 && h > 0) {
        frame.image = QImage(w, h, QImage::Format_RGB32);
        frame.image.fill(QColor(30, 60, 120));
        frame.sourceSize = QSize(w, h);
    }
    frame.timestamp = 1000;
    return frame;
}

/** Tạo một TranslationRegion với normalized rect cho trước */
static TranslationRegion makeRegion(const QString& id, const QString& name,
                                    double x, double y, double w, double h,
                                    bool enabled = true)
{
    TranslationRegion r;
    r.id = id;
    r.name = name;
    r.orderNumber = 1;
    r.normalizedRect = {x, y, w, h};
    r.enabled = enabled;
    return r;
}

// ── Test class ────────────────────────────────────────────────────────────────

class TestRegionManager : public QObject
{
    Q_OBJECT

private slots:

    // ── T1: Normalized → pixel đúng tại 1920×1080 ────────────────────────────
    void test_normalizedToPixel_1920x1080()
    {
        // NormalizedRect: x=0.20, y=0.70, w=0.60, h=0.15
        // Expected: x≈384, y≈756, w≈1152, h≈162
        NormalizedRect norm{0.20, 0.70, 0.60, 0.15};
        QRect result = RegionManager::normalizedToPixelRect(norm, 1920, 1080);

        QVERIFY(!result.isEmpty());
        QCOMPARE(result.x(),      384);
        QCOMPARE(result.y(),      756);
        QCOMPARE(result.width(),  1152);
        QCOMPARE(result.height(), 162);
    }

    // ── T2: Scale đúng ở 1280×720 ────────────────────────────────────────────
    void test_normalizedToPixel_1280x720()
    {
        // Cùng NormalizedRect, resolution nhỏ hơn
        // Expected: x=256, y=504, w=768, h=108
        NormalizedRect norm{0.20, 0.70, 0.60, 0.15};
        QRect result = RegionManager::normalizedToPixelRect(norm, 1280, 720);

        QVERIFY(!result.isEmpty());
        QCOMPARE(result.x(),      256);
        QCOMPARE(result.y(),      504);
        QCOMPARE(result.width(),  768);
        QCOMPARE(result.height(), 108);
    }

    // ── T3: Region chạm cạnh phải và bottom – không out-of-bounds ────────────
    void test_edgeBoundary_clamp()
    {
        // x=0.5, y=0.5, w=0.5, h=0.5 → phủ đúng góc phải-dưới
        NormalizedRect norm{0.5, 0.5, 0.5, 0.5};
        QRect result = RegionManager::normalizedToPixelRect(norm, 1920, 1080);

        QVERIFY(!result.isEmpty());
        // Không vượt ra ngoài frame
        QVERIFY(result.right()  <= 1920);
        QVERIFY(result.bottom() <= 1080);
        QVERIFY(result.x() >= 0);
        QVERIFY(result.y() >= 0);
    }

    // ── T4: Invalid region (width = 0) → không extract ───────────────────────
    void test_invalidRegion_zeroWidth()
    {
        RegionManager mgr;
        mgr.addRegion(makeRegion("r1", "Bad", 0.1, 0.1, 0.0, 0.2)); // width = 0

        CapturedFrame frame = makeFrame(1920, 1080);
        auto result = mgr.extractRegions(frame);

        QCOMPARE(result.size(), 0);
    }

    // ── T5: Multiple regions → trả đúng số RegionFrame ───────────────────────
    void test_multipleRegions()
    {
        RegionManager mgr;
        mgr.addRegion(makeRegion("r1", "Dialogue",   0.22, 0.68, 0.56, 0.16));
        mgr.addRegion(makeRegion("r2", "Menu",        0.03, 0.14, 0.18, 0.66));
        mgr.addRegion(makeRegion("r3", "ItemDesc",    0.80, 0.62, 0.18, 0.20));

        CapturedFrame frame = makeFrame(1920, 1080);
        auto result = mgr.extractRegions(frame);

        QCOMPARE(result.size(), 3);
        QCOMPARE(result[0].regionId, QString("r1"));
        QCOMPARE(result[1].regionId, QString("r2"));
        QCOMPARE(result[2].regionId, QString("r3"));
    }

    // ── T6: Disabled region → không extract ──────────────────────────────────
    void test_disabledRegion()
    {
        RegionManager mgr;
        mgr.addRegion(makeRegion("r1", "Active",   0.1, 0.1, 0.3, 0.2, true));
        mgr.addRegion(makeRegion("r2", "Disabled", 0.5, 0.5, 0.3, 0.2, false));

        CapturedFrame frame = makeFrame(1920, 1080);
        auto result = mgr.extractRegions(frame);

        QCOMPARE(result.size(), 1);
        QCOMPARE(result[0].regionId, QString("r1"));
    }

    // ── T7: Region hơi vượt boundary do float → clamp, không crash ───────────
    void test_floatBoundaryClamp()
    {
        // x + width = 1.005 (sai số float nhỏ do drag UI)
        NormalizedRect norm{0.0, 0.0, 1.005, 0.5};
        // Phải qua strict validation (< kRejectThreshold = 0.5) và được clamp
        QRect result = RegionManager::normalizedToPixelRect(norm, 1920, 1080);
        QVERIFY(!result.isEmpty());
        QVERIFY(result.right() <= 1920);
    }

    // ── T8: Empty/null CapturedFrame → trả empty list, không crash ───────────
    void test_emptyFrame()
    {
        RegionManager mgr;
        mgr.addRegion(makeRegion("r1", "Test", 0.1, 0.1, 0.3, 0.3));

        CapturedFrame emptyFrame; // image = null
        auto result = mgr.extractRegions(emptyFrame);

        QCOMPARE(result.size(), 0);
    }

    // ── Bonus: RegionFrame fields đúng ───────────────────────────────────────
    void test_regionFrameFields()
    {
        RegionManager mgr;
        NormalizedRect norm{0.20, 0.70, 0.60, 0.15};
        mgr.addRegion(makeRegion("roi_1", "Dialogue", norm.x, norm.y, norm.width, norm.height));

        CapturedFrame frame = makeFrame(1920, 1080);
        frame.timestamp = 12345678;
        auto result = mgr.extractRegions(frame);

        QCOMPARE(result.size(), 1);
        const RegionFrame& rf = result[0];

        QCOMPARE(rf.regionId,         QString("roi_1"));
        QCOMPARE(rf.regionName,       QString("Dialogue"));
        QCOMPARE(rf.sourceFrameSize,  QSize(1920, 1080));
        QCOMPARE(rf.timestamp,        qint64(12345678));
        QVERIFY(rf.pixelRect.width()  > 0);
        QVERIFY(rf.pixelRect.height() > 0);
        QVERIFY(!rf.image.isNull());
        QCOMPARE(rf.image.width(),    rf.pixelRect.width());
        QCOMPARE(rf.image.height(),   rf.pixelRect.height());
        QVERIFY(rf.isValid());
    }

    // ── Bonus: setRegions / removeRegion / clear ──────────────────────────────
    void test_manageRegions()
    {
        RegionManager mgr;
        mgr.addRegion(makeRegion("a", "A", 0.1, 0.1, 0.2, 0.2));
        mgr.addRegion(makeRegion("b", "B", 0.3, 0.3, 0.2, 0.2));

        QCOMPARE(mgr.count(), 2);

        bool removed = mgr.removeRegion("a");
        QVERIFY(removed);
        QCOMPARE(mgr.count(), 1);
        QCOMPARE(mgr.regions()[0].id, QString("b"));

        mgr.clear();
        QCOMPARE(mgr.count(), 0);
        QVERIFY(mgr.isEmpty());
    }
};

QTEST_MAIN(TestRegionManager)
#include "test_region_manager.moc"
