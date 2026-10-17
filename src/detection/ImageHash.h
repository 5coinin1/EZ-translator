#pragma once

#include <QImage>
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

} // namespace EZTranslator
