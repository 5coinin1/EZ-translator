#include "detection/ChangeDetector.h"

#include "detection/ImageHash.h"

namespace EZTranslator {

quint64 ChangeDetector::differenceHash(const QImage& image)
{
    return imageDifferenceHash(image);
}

int ChangeDetector::hammingDistance(quint64 a, quint64 b) noexcept
{
    return imageHammingDistance(a, b);
}

bool ChangeDetector::hasChanged(const QString& regionId, const QImage& image)
{
    const quint64 hash = imageDifferenceHash(image);

    const auto it = m_lastHash.constFind(regionId);
    if (it == m_lastHash.constEnd()) {
        m_lastHash.insert(regionId, hash);
        return true; // lần đầu tiên → coi như thay đổi để xử lý ngay
    }

    const bool changed = imageHammingDistance(it.value(), hash) > m_options.maxDistance;
    m_lastHash[regionId] = hash;
    return changed;
}

} // namespace EZTranslator
