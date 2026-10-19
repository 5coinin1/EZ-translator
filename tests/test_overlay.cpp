/**
 * @file test_overlay.cpp
 * @brief Unit tests cho logic overlay mới port từ bản translator:
 *
 *   O1 - ParagraphAnchor: đoạn bị lệch do assembler rơi/gộp dòng phải tự kéo về đúng dòng
 *   O2 - ParagraphAnchor: đặt đúng thì giữ nguyên; lệch XUỐNG cũng kéo lùi được
 *   O3 - TextCover: xoá glyph nhưng giữ nền (không tô đè cả box)
 */

#include <QtTest/QtTest>

#include <QFont>
#include <QImage>
#include <QPainter>

#include "overlay/ParagraphAnchor.h"
#include "overlay/TextCover.h"
#include "overlay/TextOverlay.h"

namespace {

int darkPixels(const QImage& image)
{
    // Chỉ đếm pixel ĐỤC (alpha cao): rìa plate mờ dần về trong suốt có RGB=0 và không phải "mực".
    int count = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QRgb pixel = image.pixel(x, y);
            if (qAlpha(pixel) > 128 && qGray(pixel) < 100)
                ++count;
        }
    }
    return count;
}

} // namespace

class TestOverlay : public QObject
{
    Q_OBJECT

private slots:

    // ── O1 ──────────────────────────────────────────────────────────────────
    void anchor_findsParagraphAfterDroppedLine()
    {
        // Chỉ số cộng dồn nói đoạn 1 chiếm 1 dòng, nhưng thực tế assembler rơi một dòng nên đoạn
        // nằm ở dòng 2. Neo nội dung phải tìm ra; chỉ số cũ sẽ đặt sai và không gì phát hiện.
        const QStringList lines = {
            QStringLiteral("The gate opened slowly."),
            QStringLiteral("A cold wind came through the hall."),
            QStringLiteral("We leave at dawn, he said."),
            QStringLiteral("The horses are ready."),
        };
        const QStringList paragraphs = {
            QStringLiteral("We leave at dawn, he said. The horses are ready."),
        };
        const QList<int> starts = ParagraphAnchor::anchor(paragraphs, lines, {1});
        QCOMPARE(starts.size(), 1);
        QCOMPARE(starts.at(0), 2);
    }

    // ── O2 ──────────────────────────────────────────────────────────────────
    void anchor_keepsCorrectAndPullsBackward()
    {
        const QStringList lines = {
            QStringLiteral("First paragraph line one."),
            QStringLiteral("First paragraph line two."),
            QStringLiteral("Second paragraph starts here."),
            QStringLiteral("Second paragraph continues."),
        };
        const QStringList paragraphs = {
            QStringLiteral("First paragraph line one. First paragraph line two."),
            QStringLiteral("Second paragraph starts here. Second paragraph continues."),
        };

        const QList<int> ok = ParagraphAnchor::anchor(paragraphs, lines, {2, 2});
        QCOMPARE(ok.at(0), 0);
        QCOMPARE(ok.at(1), 2);

        // Số dòng lệch xuống (3 thay vì 2): neo phải kéo đoạn 2 LÙI về dòng 2.
        const QList<int> drifted = ParagraphAnchor::anchor(paragraphs, lines, {3, 1});
        QCOMPARE(drifted.at(1), 2);
    }

    // ── O3 ──────────────────────────────────────────────────────────────────
    void cover_erasesGlyphsKeepsBackground()
    {
        QImage image(240, 80, QImage::Format_RGB32);
        image.fill(QColor(230, 230, 230));
        {
            QPainter painter(&image);
            painter.setPen(Qt::black);
            QFont font;
            font.setPixelSize(30);
            font.setBold(true);
            painter.setFont(font);
            painter.drawText(QRect(10, 10, 220, 56), Qt::AlignLeft | Qt::AlignVCenter,
                             QStringLiteral("HELLO"));
        }

        const int before = darkPixels(image);
        QVERIFY2(before > 50, "ảnh test phải có glyph đen");

        const QImage plate = TextCover::cleanPlate(image, image.rect(), {QRect(8, 8, 224, 60)});
        QVERIFY2(!plate.isNull(), "cleanPlate phải trả ảnh");

        const int after = darkPixels(plate);
        QVERIFY2(after * 4 < before, "glyph phải bị xoá gần hết");

        // Nền trong vùng box (phía sau chữ, không có glyph) vẫn sáng và đục — tức là không tô đè
        // cả box như cách "fill màu đặc" cũ. Rìa plate mờ dần về trong suốt là thiết kế (ngoài
        // vùng chữ thì để UI gốc hiện nguyên).
        const QRgb sample = plate.pixel(210, 40);
        QVERIFY2(qAlpha(sample) > 128, "vùng nền trong box phải đục");
        QVERIFY2(qGray(sample) > 180, "nền trong box phải giữ nguyên sáng");
    }

    // ── O4 ──────────────────────────────────────────────────────────────────
    void overlay_drawsAllTextsAboveSharedPlate()
    {
        // Regression: mọi box của một vùng dùng CHUNG một plate. Vẽ che-rồi-chữ trong cùng vòng lặp
        // làm plate của box sau đè lên CHỮ của box trước ("chữ nằm dưới layout, không nhìn thấy").
        // Hai box ở hai nửa trên/dưới: cả hai nửa đều phải còn chữ.
        EZTranslator::TextOverlay overlay;

        QImage plate(80, 40, QImage::Format_ARGB32_Premultiplied);
        plate.fill(QColor(255, 255, 255, 255)); // plate đục kín: che được mọi thứ vẽ trước nó

        QList<EZTranslator::OverlayBox> boxes;
        for (int i = 0; i < 2; ++i) {
            EZTranslator::OverlayBox box;
            box.rect = QRect(0, i * 20, 80, 20);
            box.text = QStringLiteral("HELLO");
            box.fontPixelSize = 14;
            box.textColor = QColor(0, 0, 0);
            box.plate = plate;
            box.plateRect = QRect(0, 0, 80, 40);
            boxes.append(box);
        }

        overlay.showOverTarget(QRect(0, 0, 80, 40));
        overlay.setBoxes(boxes, QSize(80, 40));

        QImage rendered(80, 40, QImage::Format_ARGB32);
        rendered.fill(Qt::transparent);
        overlay.render(&rendered);

        const auto darkInHalf = [&rendered](int y0) {
            int count = 0;
            for (int y = y0; y < y0 + 20; ++y) {
                for (int x = 0; x < rendered.width(); ++x) {
                    const QRgb pixel = rendered.pixel(x, y);
                    if (qAlpha(pixel) > 128 && qGray(pixel) < 100)
                        ++count;
                }
            }
            return count;
        };
        QVERIFY2(darkInHalf(0) > 10, "chữ của box ĐẦU bị plate của box sau đè mất");
        QVERIFY2(darkInHalf(20) > 10, "chữ của box SAU không hiện");
        overlay.hideOverlay();
    }
};

QTEST_MAIN(TestOverlay)
#include "test_overlay.moc"
