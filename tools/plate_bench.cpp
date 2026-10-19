// Đo thời gian dựng smart-cover plate (TextCover::cleanPlate) trên ảnh tổng hợp ~vùng người dùng
// (1200×700, 18 dòng chữ). Kết quả ghi ra plate_bench.out.txt.
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QGuiApplication>
#include <QImage>
#include <QList>
#include <QPainter>
#include <QRect>
#include <QTextStream>

#include "overlay/TextCover.h"

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);

    QImage image(1200, 700, QImage::Format_RGB32);
    image.fill(QColor(245, 244, 240));
    QList<QRect> rects;
    {
        QPainter painter(&image);
        painter.setPen(QColor(30, 30, 30));
        QFont font = painter.font();
        font.setPixelSize(22);
        painter.setFont(font);
        for (int i = 0; i < 18; ++i) {
            const int y = 40 + i * 36;
            const QString text = QStringLiteral("Line %1 - the quick brown fox jumps over the lazy dog")
                                     .arg(i + 1);
            painter.drawText(40, y, text);
            const QRect bound = painter.boundingRect(40, y - 22, 1100, 30,
                                                     Qt::AlignLeft | Qt::AlignVCenter, text);
            rects.append(bound.adjusted(-2, -4, 2, 4));
        }
        painter.end();
    }

    // Lượt đầu để warm cache CPU, rồi đo 3 lượt.
    QElapsedTimer timer;
    timer.start();
    QImage plate = TextCover::cleanPlate(image, image.rect(), rects);
    const qint64 warm = timer.elapsed();

    qint64 best = -1;
    for (int i = 0; i < 3; ++i) {
        timer.restart();
        plate = TextCover::cleanPlate(image, image.rect(), rects);
        const qint64 ms = timer.elapsed();
        if (best < 0 || ms < best)
            best = ms;
    }

    QFile out(QStringLiteral("plate_bench.out.txt"));
    if (out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QTextStream(&out) << "cleanPlate 18 dong (1200x700): warm=" << warm << " ms | best=" << best
                          << " ms | plate " << plate.width() << "x" << plate.height() << "\n";
    }
    return 0;
}
