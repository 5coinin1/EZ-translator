#pragma once

#include <QString>
#include <QStringList>

namespace EZTranslator {
namespace LanguageDetect {

/** True cho sentinel "tự nhận diện ngôn ngữ nguồn" ("auto" hoặc rỗng). */
[[nodiscard]] bool isAuto(const QString& code);

/**
 * @brief Đoán ngôn ngữ nguồn từ văn bản OCR, dựa trên **hệ chữ Unicode**.
 *
 * Rẻ, tất định, không cần từ điển/mạng. Trả về "en"/"zh"/"ja"/"ko"/"ru"/"ar"/
 * "he"/"th", hoặc rỗng nếu không nhận ra hệ chữ nào.
 *
 * Thận trọng: chỉ nhận "ja" khi có kana (kanji dùng chung với tiếng Trung); nếu
 * @p candidates được cho và đoán không nằm trong đó thì trả rỗng ("vẫn chưa biết").
 */
[[nodiscard]] QString detect(const QString& text, const QStringList& candidates = {});

} // namespace LanguageDetect
} // namespace EZTranslator
