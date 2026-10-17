#include "ocr/OcrScheduler.h"

#include "detection/ImageHash.h"

#include <algorithm>

namespace EZTranslator {

OcrScheduleDecision OcrScheduler::process(const QString& regionId, const QImage& image,
                                          qint64 nowMs)
{
    OcrScheduleDecision decision;
    const quint64 hash = imageDifferenceHash(image);

    Region& region = m_regions[regionId];

    // Lần đầu thấy vùng: chưa biết nội dung ổn định chưa → chờ settle.
    if (!region.known) {
        region.hash = hash;
        region.changedAt = nowMs;
        region.known = true;
        decision.changed = true;
        decision.deferred = true;
        return decision;
    }

    const bool changed = imageHammingDistance(region.hash, hash) > m_options.maxDistance;
    if (changed) {
        region.hash = hash;
        region.changedAt = nowMs;
        decision.changed = true;
        decision.deferred = true;
        return decision;
    }

    // Nội dung không đổi: chỉ chạy khi đã settle đủ lâu và không bị cap/cooldown.
    if (nowMs - region.changedAt < m_options.settleMs) {
        decision.deferred = true;
        return decision;
    }
    if (m_runsThisTick >= m_options.maxRunsPerTick) {
        decision.deferred = true;
        return decision;
    }
    if (region.lastRunMs >= 0 && nowMs - region.lastRunMs < m_options.minRunIntervalMs) {
        decision.deferred = true;
        return decision;
    }

    region.lastRunMs = nowMs;
    ++m_runsThisTick;
    decision.run = true;
    return decision;
}

} // namespace EZTranslator
