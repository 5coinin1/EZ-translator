#include "ocr/OcrThreadBudget.h"

#include <QString>
#include <QThread>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace EZTranslator {

namespace {

int positiveOverride(const char* name, int fallback, int maximum)
{
    bool ok = false;
    const int value = qEnvironmentVariableIntValue(name, &ok);
    return ok && value > 0 ? qBound(1, value, maximum) : fallback;
}

quint64 totalPhysicalRamMb()
{
    MEMORYSTATUSEX status = {};
    status.dwLength = sizeof(status);
    if (!GlobalMemoryStatusEx(&status))
        return 0;
    return quint64(status.ullTotalPhys / (1024ull * 1024ull));
}

void applyProfileSettings(OcrResourcePlan& plan, ResourceProfile profile)
{
    plan.profile = profile;
    if (profile == ResourceProfile::Low) {
        plan.ocrSettleMs = 40;
        plan.ocrCooldownMs = 180;
        plan.motionWindowMs = 140;
        plan.captureFpsCap = 20;
        plan.lineCacheLimit = 1024;
        plan.ocrBoxCachePerRegion = 4;
        plan.translationCacheLines = 1500;
        plan.paragraphCacheLines = 1500;
    } else if (profile == ResourceProfile::High) {
        plan.ocrSettleMs = 15;
        plan.ocrCooldownMs = 60;
        plan.motionWindowMs = 80;
        plan.captureFpsCap = 30;
        plan.lineCacheLimit = 4096;
        plan.ocrBoxCachePerRegion = 12;
        plan.translationCacheLines = 5000;
        plan.paragraphCacheLines = 5000;
    } else {
        plan.ocrSettleMs = 20;
        plan.ocrCooldownMs = 90;
        plan.motionWindowMs = 100;
        plan.captureFpsCap = 30;
        plan.lineCacheLimit = 2048;
        plan.ocrBoxCachePerRegion = 8;
        plan.translationCacheLines = 3000;
        plan.paragraphCacheLines = 3000;
    }
}

} // namespace

OcrResourcePlan detectOcrResourcePlan(int gpuVramMb)
{
    OcrResourcePlan plan;
    plan.logicalThreads = qMax(1, QThread::idealThreadCount());
    plan.totalRamMb = totalPhysicalRamMb();
    plan.gpuVramMb = qMax(0, gpuVramMb);

    const bool lowMemory = plan.totalRamMb > 0 && plan.totalRamMb < 8192;
    const bool weakGpu = plan.gpuVramMb > 0 && plan.gpuVramMb < 2048;
    const bool low = plan.logicalThreads <= 4 || lowMemory || weakGpu;
    const bool high = plan.logicalThreads >= 12
                      && (plan.totalRamMb == 0 || plan.totalRamMb >= 15000)
                      && (plan.gpuVramMb == 0 || plan.gpuVramMb >= 4096);

    ResourceProfile profile = low ? ResourceProfile::Low
                                  : high ? ResourceProfile::High
                                         : ResourceProfile::Balanced;

    // Cho phép ép profile khi test/benchmark hoặc khi người dùng muốn ưu tiên tiết kiệm.
    const QString override = qEnvironmentVariable("EZ_RESOURCE_PROFILE").trimmed().toLower();
    if (override == QLatin1String("low"))
        profile = ResourceProfile::Low;
    else if (override == QLatin1String("high"))
        profile = ResourceProfile::High;
    else if (override == QLatin1String("balanced"))
        profile = ResourceProfile::Balanced;

    applyProfileSettings(plan, profile);
    return plan;
}

OcrThreadBudget chooseOcrThreadBudget(bool gpu, bool hoverWorker, ResourceProfile profile)
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
        const bool low = profile == ResourceProfile::Low;
        // Giữ một phần CPU cho UI/capture/translation. Recognition chạy nhiều session
        // độc lập với intra-op=1; máy yếu chỉ 1 det thread + 2 worker rec. Máy
        // nhiều core được dùng tối đa 6 worker (vẫn chừa core cho UI/dịch), thay vì
        // chặn cứng 4 như trước — full-page CPU là điểm nghẽn chính.
        budget.detectorIntraOp = low ? 1 : qBound(1, logicalThreads / 8, 2);
        budget.recognizerIntraOp = 1;
        // Model CPU int8 Conv đã bão hoà quanh 4 worker; không cần 6-10 worker.
        // Giữ 4 cho balanced/high, 2 cho máy yếu — ít CPU hơn mà full vẫn nhanh.
        int workers = low ? (logicalThreads <= 2 ? 1 : 2)
                          : logicalThreads <= 2 ? 1 : qBound(2, logicalThreads / 2, 4);
        budget.recognizerWorkers = workers;
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

int chooseOcrRecMaxWidth(bool gpu, int gpuVramMb)
{
    if (!gpu)
        return 480;
    if (gpuVramMb > 0 && gpuVramMb < 1024)
        return 960;
    if (gpuVramMb > 0 && gpuVramMb < 2048)
        return 1280;
    return 1920;
}

} // namespace EZTranslator
