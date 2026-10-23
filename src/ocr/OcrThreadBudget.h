#pragma once

namespace EZTranslator {

/**
 * @brief Ngân sách thread cho một OCR worker.
 *
 * Budget này được tính một lần khi tạo options. Mỗi worker OCR nhận một phần
 * budget riêng để region worker và hover worker không cùng tự mở quá nhiều
 * ORT/recognizer thread. Có thể override bằng biến môi trường để A/B benchmark.
 */
struct OcrThreadBudget
{
    int detectorIntraOp{1};
    int recognizerIntraOp{1};
    int recognizerWorkers{1};
    int interOp{1};
};

[[nodiscard]] OcrThreadBudget chooseOcrThreadBudget(bool gpu, bool hoverWorker);

} // namespace EZTranslator
