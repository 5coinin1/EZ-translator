#include "ocr/OcrThreadBudget.h"

#include <QThread>

namespace EZTranslator {

namespace {

int positiveOverride(const char* name, int fallback, int maximum)
{
    bool ok = false;
    const int value = qEnvironmentVariableIntValue(name, &ok);
    return ok && value > 0 ? qBound(1, value, maximum) : fallback;
}

} // namespace

OcrThreadBudget chooseOcrThreadBudget(bool gpu, bool hoverWorker)
{
    OcrThreadBudget budget;
    const int logicalThreads = qMax(1, QThread::idealThreadCount());

    if (gpu) {
        // DirectML/CUDA không được chạy nhiều Run song song trên cùng session.
        budget.detectorIntraOp = 1;
        budget.recognizerIntraOp = 1;
        budget.recognizerWorkers = 1;
        budget.interOp = 1;
    } else {
        // Giữ lại CPU cho UI/capture/translation. Với máy 16 logical threads,
        // profile mặc định là detector 2 + recognizer 4 worker x 1 intra-op.
        budget.detectorIntraOp = qBound(1, logicalThreads / 8, 2);
        budget.recognizerIntraOp = 1;
        budget.recognizerWorkers = logicalThreads <= 2
                                       ? 1
                                       : qBound(2, logicalThreads / 4, 4);
        budget.interOp = 1;
        if (hoverWorker) {
            // Hover thường chỉ có một dòng; không dành cả pool CPU cho nó.
            budget.detectorIntraOp = 1;
            budget.recognizerWorkers = 1;
        }
    }

    budget.detectorIntraOp = positiveOverride("EZ_OCR_DET_THREADS",
                                               budget.detectorIntraOp, 16);
    budget.recognizerIntraOp = positiveOverride("EZ_OCR_REC_THREADS",
                                                 budget.recognizerIntraOp, 16);
    budget.recognizerWorkers = positiveOverride("EZ_OCR_REC_WORKERS",
                                                 budget.recognizerWorkers, 16);
    budget.interOp = positiveOverride("EZ_OCR_INTER_THREADS", budget.interOp, 8);
    return budget;
}

} // namespace EZTranslator
