#pragma once

#include <QString>

namespace EZTranslator {
namespace TranslationPost {

/**
 * @brief Gộp các từ/cụm bị lặp do model sinh lỗi ("đau đau đau đau đớn").
 *
 * Chỉ gộp khi một từ lặp >= 3 lần (hoặc cụm lặp >= 2 lần) để giữ nguyên hiện
 * tượng láy thật của tiếng Việt ("đỏ đỏ", "xanh xanh").
 */
[[nodiscard]] QString collapseRepeats(const QString& text);

/**
 * @brief Giữ dấu câu/đánh dấu hội thoại của bản GỐC trên bản dịch.
 *
 * Ví dụ nguồn "- Hello!" mà model dịch mất gạch đầu dòng/dấu chấm than thì khôi
 * phục lại từ nguồn (chỉ áp phần đầu/cuối, không đụng phần giữa).
 */
[[nodiscard]] QString applyPunctuation(const QString& source, const QString& translation);

/**
 * @brief Bản dịch có dùng được không (lọc output rác của model).
 *
 * Loại: rỗng; echo y hệt nguồn; một ký tự lặp; quá ngắn so với nguồn; quá dài
 * bất thường (hallucination).
 */
/**
 * @brief Doi dau cau CJK (。！？，：；、) sang dau Latin khi dich sang ngon ngu
 * khong dung chu Han/Nhat. Google/offline doi khi giu nguyen dau cau nguon.
 */
[[nodiscard]] QString normalizePunctuation(const QString& text, const QString& target);

[[nodiscard]] bool isUsableTranslation(const QString& source, const QString& translation);

} // namespace TranslationPost
} // namespace EZTranslator
