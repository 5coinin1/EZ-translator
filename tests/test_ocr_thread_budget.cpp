#include <QtTest/QtTest>

#include "ocr/OcrThreadBudget.h"
#include "translation/TranslationThreadBudget.h"

using namespace EZTranslator;

class EnvironmentGuard
{
public:
    explicit EnvironmentGuard(const char* name)
        : m_name(name), m_wasSet(qEnvironmentVariableIsSet(name)), m_value(qgetenv(name))
    {
    }

    ~EnvironmentGuard()
    {
        if (m_wasSet)
            qputenv(m_name.constData(), m_value);
        else
            qunsetenv(m_name.constData());
    }

private:
    QByteArray m_name;
    bool m_wasSet{false};
    QByteArray m_value;
};

class TestOcrThreadBudget : public QObject
{
    Q_OBJECT

private slots:
    void defaultProfilesAreBounded()
    {
        const EnvironmentGuard det("EZ_OCR_DET_THREADS");
        const EnvironmentGuard rec("EZ_OCR_REC_THREADS");
        const EnvironmentGuard workers("EZ_OCR_REC_WORKERS");
        const EnvironmentGuard inter("EZ_OCR_INTER_THREADS");
        qunsetenv("EZ_OCR_DET_THREADS");
        qunsetenv("EZ_OCR_REC_THREADS");
        qunsetenv("EZ_OCR_REC_WORKERS");
        qunsetenv("EZ_OCR_INTER_THREADS");

        const OcrThreadBudget cpu = chooseOcrThreadBudget(false, false);
        QVERIFY(cpu.detectorIntraOp >= 1 && cpu.detectorIntraOp <= 2);
        QCOMPARE(cpu.recognizerIntraOp, 1);
        QVERIFY(cpu.recognizerWorkers >= 1 && cpu.recognizerWorkers <= 4);
        QCOMPARE(cpu.interOp, 1);

        const OcrThreadBudget hover = chooseOcrThreadBudget(false, true);
        QCOMPARE(hover.detectorIntraOp, 1);
        QCOMPARE(hover.recognizerWorkers, 1);

        const OcrThreadBudget gpu = chooseOcrThreadBudget(true, false);
        QCOMPARE(gpu.detectorIntraOp, 1);
        QCOMPARE(gpu.recognizerIntraOp, 1);
        QCOMPARE(gpu.recognizerWorkers, 1);
        QCOMPARE(gpu.interOp, 1);
    }

    void environmentOverridesAreAppliedAndClamped()
    {
        const EnvironmentGuard det("EZ_OCR_DET_THREADS");
        const EnvironmentGuard rec("EZ_OCR_REC_THREADS");
        const EnvironmentGuard workers("EZ_OCR_REC_WORKERS");
        const EnvironmentGuard inter("EZ_OCR_INTER_THREADS");
        qputenv("EZ_OCR_DET_THREADS", "3");
        qputenv("EZ_OCR_REC_THREADS", "5");
        qputenv("EZ_OCR_REC_WORKERS", "20");
        qputenv("EZ_OCR_INTER_THREADS", "2");

        const OcrThreadBudget budget = chooseOcrThreadBudget(false, false);
        QCOMPARE(budget.detectorIntraOp, 3);
        QCOMPARE(budget.recognizerIntraOp, 5);
        QCOMPARE(budget.recognizerWorkers, 16);
        QCOMPARE(budget.interOp, 2);
    }

    void translationBudgetHasAnOverride()
    {
        const EnvironmentGuard threads("EZ_TRANSLATE_THREADS");
        qunsetenv("EZ_TRANSLATE_THREADS");
        const int fallback = chooseTranslationThreads();
        QVERIFY(fallback >= 1 && fallback <= 16);

        qputenv("EZ_TRANSLATE_THREADS", "7");
        QCOMPARE(chooseTranslationThreads(), 7);
    }
};

QTEST_MAIN(TestOcrThreadBudget)
#include "test_ocr_thread_budget.moc"
