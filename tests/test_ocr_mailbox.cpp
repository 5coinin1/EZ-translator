#include <QtTest/QtTest>

#include "ocr/OcrRegionMailbox.h"

using namespace EZTranslator;

class TestOcrMailbox : public QObject
{
    Q_OBJECT

private:
    static OcrRegionMailbox::Request request(quint64 token)
    {
        return {QImage(8, 8, QImage::Format_RGB32), token, token * 17};
    }

private slots:
    void latestWinsBoundsBurst()
    {
        OcrRegionMailbox mailbox;
        QCOMPARE(mailbox.submit(QStringLiteral("r1"), request(1)),
                 OcrRegionMailbox::SubmitResult::Queued);

        const auto first = mailbox.startNext();
        QVERIFY(first.has_value());
        QCOMPARE(first->regionId, QStringLiteral("r1"));
        QCOMPARE(first->request.requestToken, quint64(1));

        for (quint64 token = 2; token <= 51; ++token) {
            QCOMPARE(mailbox.submit(QStringLiteral("r1"), request(token)),
                     OcrRegionMailbox::SubmitResult::Coalesced);
            QCOMPARE(mailbox.pendingCount(), 1);
            QCOMPARE(mailbox.readyCount(), 0);
        }

        mailbox.finish(QStringLiteral("r1"));
        const auto latest = mailbox.startNext();
        QVERIFY(latest.has_value());
        QCOMPARE(latest->request.requestToken, quint64(51));
        QCOMPARE(mailbox.pendingCount(), 0);
        QCOMPARE(mailbox.readyCount(), 0);
    }

    void roundRobinPreventsRegionStarvation()
    {
        OcrRegionMailbox mailbox;
        mailbox.submit(QStringLiteral("r1"), request(1));
        const auto first = mailbox.startNext();
        QVERIFY(first.has_value());

        mailbox.submit(QStringLiteral("r1"), request(2));
        mailbox.submit(QStringLiteral("r2"), request(20));
        mailbox.submit(QStringLiteral("r3"), request(30));

        mailbox.finish(QStringLiteral("r1"));
        const auto second = mailbox.startNext();
        QVERIFY(second.has_value());
        QCOMPARE(second->regionId, QStringLiteral("r2"));

        mailbox.finish(QStringLiteral("r2"));
        const auto third = mailbox.startNext();
        QVERIFY(third.has_value());
        QCOMPARE(third->regionId, QStringLiteral("r3"));

        mailbox.finish(QStringLiteral("r3"));
        const auto roundRobin = mailbox.startNext();
        QVERIFY(roundRobin.has_value());
        QCOMPARE(roundRobin->regionId, QStringLiteral("r1"));
        QCOMPARE(roundRobin->request.requestToken, quint64(2));
    }

    void removeAndClearKeepActiveLifecycleSafe()
    {
        OcrRegionMailbox mailbox;
        mailbox.submit(QStringLiteral("active"), request(1));
        const auto active = mailbox.startNext();
        QVERIFY(active.has_value());

        mailbox.submit(QStringLiteral("active"), request(2));
        mailbox.submit(QStringLiteral("removed"), request(3));
        mailbox.remove(QStringLiteral("removed"));
        QCOMPARE(mailbox.pendingCount(), 1);
        QVERIFY(mailbox.isActive(QStringLiteral("active")));

        mailbox.clearPending();
        QCOMPARE(mailbox.pendingCount(), 0);
        QVERIFY(mailbox.isActive(QStringLiteral("active")));

        mailbox.finish(QStringLiteral("active"));
        QVERIFY(!mailbox.hasActive());
        QVERIFY(!mailbox.startNext().has_value());

        mailbox.clear();
        QVERIFY(!mailbox.hasActive());
    }
};

QTEST_MAIN(TestOcrMailbox)
#include "test_ocr_mailbox.moc"
