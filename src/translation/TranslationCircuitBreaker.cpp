#include "translation/TranslationCircuitBreaker.h"

#include <algorithm>

namespace EZTranslator {

TranslationCircuitBreaker::TranslationCircuitBreaker(qint64 rateLimitBaseMs,
                                                     qint64 rateLimitMaxMs,
                                                     qint64 transientCooldownMs)
    : m_rateLimitBaseMs(std::max<qint64>(1, rateLimitBaseMs))
    , m_rateLimitMaxMs(std::max(m_rateLimitBaseMs, rateLimitMaxMs))
    , m_transientCooldownMs(std::max<qint64>(1, transientCooldownMs))
{
}

bool TranslationCircuitBreaker::allow(qint64 nowMs) const
{
    return nowMs >= m_blockedUntilMs;
}

void TranslationCircuitBreaker::recordSuccess()
{
    m_blockedUntilMs = 0;
    m_consecutiveRateLimits = 0;
}

void TranslationCircuitBreaker::recordFailure(Failure failure, qint64 nowMs,
                                              qint64 retryAfterMs)
{
    qint64 cooldown = m_transientCooldownMs;
    if (failure == Failure::RateLimited) {
        ++m_consecutiveRateLimits;
        const int shift = std::min(m_consecutiveRateLimits - 1, 3);
        const qint64 exponential = m_rateLimitBaseMs * (qint64(1) << shift);
        cooldown = std::min(m_rateLimitMaxMs, exponential);
        if (retryAfterMs > 0)
            cooldown = std::clamp(retryAfterMs, qint64(1000), m_rateLimitMaxMs);
    } else {
        // Một lỗi mạng không xoá chuỗi 429: nhà cung cấp vẫn có thể đang giới hạn.
        cooldown = std::min(m_rateLimitMaxMs, m_transientCooldownMs);
    }
    m_blockedUntilMs = std::max(m_blockedUntilMs, nowMs + cooldown);
}

} // namespace EZTranslator
