#include "detection/ChangeDetector.h"

#include <bit>

namespace EZTranslator {

quint64 ChangeDetector::differenceHash(const QImage& image)
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

int ChangeDetector::hammingDistance(quint64 a, quint64 b) noexcept
{
    return std::popcount(a ^ b);
}

bool ChangeDetector::hasChanged(const QString& regionId, const QImage& image)
{
    const quint64 hash = differenceHash(image);

    const auto it = m_lastHash.constFind(regionId);
    if (it == m_lastHash.constEnd()) {
        m_lastHash.insert(regionId, hash);
        return true; // lần đầu tiên → coi như thay đổi để xử lý ngay
    }

    const bool changed = hammingDistance(it.value(), hash) > m_options.maxDistance;
    m_lastHash[regionId] = hash;
    return changed;
}

} // namespace EZTranslator
