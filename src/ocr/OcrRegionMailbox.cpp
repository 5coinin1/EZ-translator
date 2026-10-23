#include "ocr/OcrRegionMailbox.h"

#include <utility>

namespace EZTranslator {

OcrRegionMailbox::SubmitResult OcrRegionMailbox::submit(const QString& regionId,
                                                         Request request)
{
    const bool wasActive = isActive(regionId);
    const bool wasPending = m_pending.contains(regionId);

    m_pending.insert(regionId, std::move(request));
    if (!wasActive && !m_readyRegions.contains(regionId))
        m_readyRegions.enqueue(regionId);

    return (wasActive || wasPending) ? SubmitResult::Coalesced : SubmitResult::Queued;
}

std::optional<OcrRegionMailbox::Dispatch> OcrRegionMailbox::startNext()
{
    if (hasActive())
        return std::nullopt;

    while (!m_readyRegions.isEmpty()) {
        const QString regionId = m_readyRegions.dequeue();
        const auto pendingIt = m_pending.find(regionId);
        if (pendingIt == m_pending.end())
            continue;

        Dispatch dispatch{regionId, pendingIt.value()};
        m_pending.erase(pendingIt);
        m_activeRegion = regionId;
        return dispatch;
    }

    return std::nullopt;
}

void OcrRegionMailbox::finish(const QString& regionId)
{
    if (!isActive(regionId))
        return;

    m_activeRegion.clear();
    if (m_pending.contains(regionId) && !m_readyRegions.contains(regionId))
        m_readyRegions.enqueue(regionId);
}

void OcrRegionMailbox::remove(const QString& regionId)
{
    m_pending.remove(regionId);
    m_readyRegions.removeAll(regionId);
}

void OcrRegionMailbox::clear()
{
    m_pending.clear();
    m_readyRegions.clear();
    m_activeRegion.clear();
}

void OcrRegionMailbox::clearPending()
{
    m_pending.clear();
    m_readyRegions.clear();
}

bool OcrRegionMailbox::isActive(const QString& regionId) const
{
    return !regionId.isEmpty() && m_activeRegion == regionId;
}

} // namespace EZTranslator
