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
        // Giữ một phần CPU cho UI/capture/translation. Recognition chạy nhiều session
        // độc lập với intra-op=1; trên CPU phổ thông, tăng số worker giảm thời gian một ROI
        // rõ hơn so với tăng intra-op của từng session.
        budget.detectorIntraOp = qBound(1, logicalThreads / 8, 2);
        budget.recognizerIntraOp = 1;
        budget.recognizerWorkers = logicalThreads <= 2
                                       ? 1
                                       : qBound(2, logicalThreads / 2, 4);
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
