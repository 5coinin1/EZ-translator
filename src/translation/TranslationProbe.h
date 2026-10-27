#pragma once

#include "translation/TranslationConfig.h"

#include <QString>

namespace EZTranslator::TranslationProbe {

/**
 * @brief Kết quả một lượt "Kiểm tra engine" trong Cài đặt → Dịch thuật.
 */
struct Result
{
    bool ok{false};
    QString engine; ///< engine thực sự trả lời: "ct2", "marian", "google", "ai"
    QString detail; ///< thông báo cho người dùng (kèm bản dịch mẫu khi thành công)
};

/**
 * @brief Chạy THẬT engine đã chọn trên một câu mẫu để chứng minh cấu hình hoạt động.
 *
 * Cùng logic chọn engine với TranslationWorker (ưu tiên ct2 → Marian cho offline,
 * "auto" = offline trước rồi Google). BLOCKING (mạng/model có timeout riêng) —
 * gọi từ luồng nền, không gọi trên GUI thread.
 */
Result run(const TranslationConfig& config,
           const QString& sample = QStringLiteral("Hello, world!"));

} // namespace EZTranslator::TranslationProbe
