#include "translation/TranslationCircuitBreaker.h"

#include <QtTest>

using namespace EZTranslator;

class TranslationQosTest final : public QObject
{
    Q_OBJECT

private slots:
    void transientFailureHasShortCooldown()
    {
        TranslationCircuitBreaker breaker(100, 800, 50);
        breaker.recordFailure(TranslationCircuitBreaker::Failure::Transient, 1000);
        QVERIFY(!breaker.allow(1049));
        QVERIFY(breaker.allow(1050));
    }

    void rateLimitBacksOffAndHonorsRetryAfter()
    {
        TranslationCircuitBreaker breaker(1000, 8000, 50);
        breaker.recordFailure(TranslationCircuitBreaker::Failure::RateLimited, 1000);
        QCOMPARE(breaker.blockedUntilMs(), qint64(2000));
        breaker.recordFailure(TranslationCircuitBreaker::Failure::RateLimited, 2000);
        QCOMPARE(breaker.blockedUntilMs(), qint64(4000));
        breaker.recordFailure(TranslationCircuitBreaker::Failure::RateLimited, 4000, 7000);
        QCOMPARE(breaker.blockedUntilMs(), qint64(11000));
        QCOMPARE(breaker.consecutiveRateLimits(), 3);
    }

    void successClosesCircuit()
    {
        TranslationCircuitBreaker breaker(100, 800, 50);
        breaker.recordFailure(TranslationCircuitBreaker::Failure::RateLimited, 1000);
        QVERIFY(!breaker.allow(1050));
        breaker.recordSuccess();
        QVERIFY(breaker.allow(1050));
        QCOMPARE(breaker.consecutiveRateLimits(), 0);
    }
};

QTEST_MAIN(TranslationQosTest)
#include "test_translation_qos.moc"
