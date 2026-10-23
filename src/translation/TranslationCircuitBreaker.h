#pragma once

#include <QtGlobal>

namespace EZTranslator {

/**
 * @brief Giới hạn retry online khi nhà cung cấp bị rate-limit hoặc mạng lỗi.
 *
 * Chỉ chứa chính sách thời gian, không phụ thuộc Qt Network nên có thể test độc lập.
 */
class TranslationCircuitBreaker
{
public:
    enum class Failure
    {
        RateLimited,
        Transient,
    };

    explicit TranslationCircuitBreaker(qint64 rateLimitBaseMs = 15000,
                                       qint64 rateLimitMaxMs = 120000,
                                       qint64 transientCooldownMs = 2000);

    [[nodiscard]] bool allow(qint64 nowMs) const;
    [[nodiscard]] qint64 blockedUntilMs() const { return m_blockedUntilMs; }
    [[nodiscard]] int consecutiveRateLimits() const { return m_consecutiveRateLimits; }

    void recordSuccess();
    void recordFailure(Failure failure, qint64 nowMs, qint64 retryAfterMs = 0);

private:
    qint64 m_rateLimitBaseMs;
    qint64 m_rateLimitMaxMs;
    qint64 m_transientCooldownMs;
    qint64 m_blockedUntilMs{0};
    int m_consecutiveRateLimits{0};
};

} // namespace EZTranslator
