#include "translation/LanguageDetect.h"

#include <QChar>

namespace EZTranslator {
namespace LanguageDetect {

namespace {

// Đếm số ký tự mỗi hệ chữ; bỏ qua chữ/số Latin ASCII thường và dấu câu.
struct ScriptCounts
{
    int latin = 0;
    int hiragana = 0;
    int katakana = 0;
    int han = 0;
    int hangul = 0;
    int cyrillic = 0;
    int arabic = 0;
    int hebrew = 0;
    int thai = 0;
};

ScriptCounts countScripts(const QString& text)
{
    ScriptCounts counts;
    for (const QChar& qc : text) {
        const char32_t c = qc.unicode();

        if ((c >= U'A' && c <= U'Z') || (c >= U'a' && c <= U'z'))
            ++counts.latin;
        else if (c >= 0x00C0 && c <= 0x024F) // Latin-1 Supplement + Latin Extended-A/B
            ++counts.latin;
        else if (c >= 0x3040 && c <= 0x309F)
            ++counts.hiragana;
        else if ((c >= 0x30A0 && c <= 0x30FF) || (c >= 0xFF66 && c <= 0xFF9D))
            ++counts.katakana;
        else if ((c >= 0x4E00 && c <= 0x9FFF) || (c >= 0x3400 && c <= 0x4DBF)
                 || (c >= 0xF900 && c <= 0xFAFF))
            ++counts.han;
        else if ((c >= 0xAC00 && c <= 0xD7A3) || (c >= 0x1100 && c <= 0x11FF)
                 || (c >= 0x3130 && c <= 0x318F))
            ++counts.hangul;
        else if (c >= 0x0400 && c <= 0x04FF)
            ++counts.cyrillic;
        else if (c >= 0x0600 && c <= 0x06FF)
            ++counts.arabic;
        else if (c >= 0x0590 && c <= 0x05FF)
            ++counts.hebrew;
        else if (c >= 0x0E00 && c <= 0x0E7F)
            ++counts.thai;
    }
    return counts;
}

} // namespace

bool isAuto(const QString& code)
{
    const QString trimmed = code.trimmed();
    return trimmed.isEmpty() || trimmed.compare(QStringLiteral("auto"), Qt::CaseInsensitive) == 0;
}

QString detect(const QString& text, const QStringList& candidates)
{
    const ScriptCounts counts = countScripts(text);

    QString guess;
    if (counts.hiragana + counts.katakana > 0)
        guess = QStringLiteral("ja");
    else if (counts.hangul > 0)
        guess = QStringLiteral("ko");
    else if (counts.han > 0)
        guess = QStringLiteral("zh");
    else if (counts.cyrillic > 0)
        guess = QStringLiteral("ru");
    else if (counts.arabic > 0)
        guess = QStringLiteral("ar");
    else if (counts.hebrew > 0)
        guess = QStringLiteral("he");
    else if (counts.thai > 0)
        guess = QStringLiteral("th");
    else if (counts.latin > 0)
        guess = QStringLiteral("en");

    if (guess.isEmpty())
        return QString();
    if (!candidates.isEmpty() && !candidates.contains(guess))
        return QString();
    return guess;
}

} // namespace LanguageDetect
} // namespace EZTranslator
