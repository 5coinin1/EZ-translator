#pragma once

#include <QHash>
#include <QQueue>
#include <QString>

namespace EZTranslator {

/**
 * @brief Cache bản dịch theo nội dung (L1 RAM).
 *
 * Key = (ngôn ngữ nguồn, ngôn ngữ đích, text). Nhờ vậy khi cuộn qua lại, câu đã
 * dịch không phải gọi engine lại — vừa nhanh vừa tiết kiệm.
 *
 * Không thread-safe: dùng từ một thread.
 */
class TranslationCache
{
public:
    explicit TranslationCache(int limit = 5000) : m_limit(limit > 0 ? limit : 1) {}

    /** Trả bản dịch đã lưu, hoặc chuỗi rỗng nếu chưa có. */
    [[nodiscard]] QString get(const QString& text, const QString& from, const QString& to) const;

    /** Lưu bản dịch (bỏ qua nếu text/translation rỗng). */
    void put(const QString& text, const QString& from, const QString& to, const QString& translation);

    void setLimit(int limit);
    void clear();

    [[nodiscard]] int size() const { return m_map.size(); }

private:
    [[nodiscard]] static QString makeKey(const QString& text, const QString& from, const QString& to);

    QHash<QString, QString> m_map;
    QQueue<QString> m_order; // thứ tự chèn để evict LRU đơn giản
    int m_limit{5000};
};

} // namespace EZTranslator
