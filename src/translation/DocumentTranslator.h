#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

namespace EZTranslator {

class TranslationCache;

/** Kết quả dịch một tài liệu (một vùng) theo đoạn. */
struct TranslatedDocument
{
    QStringList sourceParagraphs; ///< text nguồn từng đoạn (đã gộp dòng, simplified)
    QStringList paragraphs;       ///< bản dịch từng đoạn (rỗng nếu chưa dịch xong)
    QList<int>  lineCounts;       ///< số dòng nguồn của mỗi đoạn (để đặt overlay)
    bool complete{false};         ///< mọi đoạn đã có bản dịch
    int  newTranslations{0};      ///< số đoạn được dịch trong lần gọi này
};

/**
 * @brief Chuyển văn bản đã assemble thành bản dịch theo ĐOẠN, có cache.
 *
 * Đơn vị dịch là một đoạn (ngăn bởi dòng trống). Đoạn đã có trong cache được
 * dùng lại ngay; chỉ những đoạn mới mới gọi engine — nhờ vậy khi cuộn chỉ trả
 * tiền cho nội dung mới thực sự xuất hiện.
 */
class DocumentTranslator
{
public:
    using TranslateFn = std::function<QStringList(const QStringList& units)>;

    explicit DocumentTranslator(TranslationCache* cache = nullptr) : m_cache(cache) {}

    [[nodiscard]] TranslatedDocument translate(const QString& assembledText, const QString& source,
                                               const QString& target, const TranslateFn& fn);

private:
    TranslationCache* m_cache{nullptr};
};

} // namespace EZTranslator
