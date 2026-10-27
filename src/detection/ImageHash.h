#pragma once

#include <QImage>
#include <QList>
#include <QRect>
#include <QtTypes>

#include <bit>

namespace EZTranslator {

/**
 * @brief difference hash (dHash) 64-bit của ảnh.
 *
 * Ảnh thu nhỏ về 9x8 grayscale; mỗi cặp pixel ngang so sánh tạo 1 bit.
 * Dùng chung cho ChangeDetector và OcrScheduler.
 *
 * @return hash; 0 nếu ảnh rỗng.
 */
[[nodiscard]] inline quint64 imageDifferenceHash(const QImage& image)
{
    if (image.isNull())
        return 0;

    const QImage small = image.convertToFormat(QImage::Format_Grayscale8)
                             .scaled(9, 8, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    quint64 hash = 0;
    int bit = 0;
    for (int y = 0; y < small.height(); ++y) {
        const uchar* line = small.constScanLine(y);
        for (int x = 0; x + 1 < small.width(); ++x, ++bit) {
            if (line[x] > line[x + 1])
                hash |= (quint64(1) << bit);
        }
    }
    return hash;
}

/** Khoảng cách Hamming giữa hai hash 64-bit (số bit khác nhau). */
[[nodiscard]] inline int imageHammingDistance(quint64 a, quint64 b) noexcept
{
    return std::popcount(a ^ b);
}

/**
 * Hash ổn định cho ROI hội thoại game: so nội dung trong các box chữ đã biết,
 * bỏ nền chuyển động bên ngoài box. Rect phải theo tọa độ ảnh hiện tại.
 */
[[nodiscard]] inline quint64 imageTextDifferenceHash(const QImage& image,
                                                     const QList<QRect>& textRects)
{
    if (image.isNull() || textRects.isEmpty())
        return imageDifferenceHash(image);

    quint64 hash = 0;
    int index = 0;
    for (const QRect& rect : textRects) {
        const int padY = qMax(6, rect.height() / 2);
        const QRect cropRect = rect.adjusted(-2, -padY, 2, padY).intersected(image.rect());
        if (cropRect.width() < 2 || cropRect.height() < 2)
            continue;
        const quint64 lineHash = imageDifferenceHash(image.copy(cropRect));
        const int rotate = (index * 11) % 64;
        hash ^= std::rotl(lineHash, rotate);

        // Giữ lại biến động số dòng/vị trí nhưng tránh avalanche hash khiến một
        // pixel nền đổi làm cả signature khác hoàn toàn.
        const quint64 geometry = quint64((cropRect.x() / 8) & 0x3ff)
                                 | (quint64((cropRect.y() / 8) & 0x3ff) << 10)
                                 | (quint64((cropRect.width() / 8) & 0x3ff) << 20)
                                 | (quint64((cropRect.height() / 8) & 0x3ff) << 30);
        hash ^= std::rotl(geometry, (index * 7 + 3) % 64);
        ++index;
    }
    hash ^= std::rotl(quint64(index) * 0x9e3779b97f4a7c15ULL, 17);
    return index > 0 ? hash : imageDifferenceHash(image);
}

} // namespace EZTranslator
