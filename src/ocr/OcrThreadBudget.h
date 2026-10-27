#pragma once

#include <QtGlobal>

namespace EZTranslator {

/** Mức tài nguyên tổng thể của máy. Dùng để chọn budget OCR/cache/fps an toàn. */
enum class ResourceProfile
{
    Low,
    Balanced,
    High,
};

/** Kế hoạch tài nguyên tính một lần lúc khởi động. */
struct OcrResourcePlan
{
    ResourceProfile profile{ResourceProfile::Balanced};
    int logicalThreads{1};
    quint64 totalRamMb{0};
    int gpuVramMb{0};

    int ocrSettleMs{20};
    int ocrCooldownMs{90};
    int motionWindowMs{100};
    int captureFpsCap{30};
    int lineCacheLimit{2048};
    int ocrBoxCachePerRegion{8};
    int translationCacheLines{3000};
    int paragraphCacheLines{3000};
};

/**
 * @brief Ngân sách thread cho một OCR worker.
 *
 * Budget này được tính một lần khi tạo options. Mỗi worker OCR nhận một phần
 * budget riêng để region worker và hover worker không cùng tỏa quá nhiều
 * ORT/recognizer thread. Có thể override bằng biến môi trường để A/B benchmark.
 */
struct OcrThreadBudget
{
    int detectorIntraOp{1};
    int recognizerIntraOp{1};
    int recognizerWorkers{1};
    int interOp{1};
};

/** @brief Phát hiện cấu hình máy (CPU/RAM/GPU) và chọn profile tài nguyên. */
[[nodiscard]] OcrResourcePlan detectOcrResourcePlan(int gpuVramMb);

/**
 * @brief Chọn budget OCR theo backend và profile.
 *
 * - GPU: DirectML/CUDA không chạy nhiều Run song song trên cùng session.
 * - CPU Low: 1 det thread, 2 worker recognition.
 * - CPU Balanced/High: det 1-2 thread, tối đa 4 worker recognition (chừa core
 *   cho UI/capture/dịch).
 * - Hover: luôn giữ 1 worker để không chiếm pool của vùng.
 */
[[nodiscard]] OcrThreadBudget chooseOcrThreadBudget(
    bool gpu, bool hoverWorker, ResourceProfile profile = ResourceProfile::Balanced);

/**
 * @brief Chiều rộng tối đa cho một dòng rec, theo backend và VRAM.
 *
 * CPU: 480 (chia dòng dài để rec nhanh hơn). GPU yếu: 960/1280; GPU đủ: 1920.
 */
[[nodiscard]] int chooseOcrRecMaxWidth(bool gpu, int gpuVramMb);

} // namespace EZTranslator
