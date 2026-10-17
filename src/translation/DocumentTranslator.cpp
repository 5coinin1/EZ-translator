#include "translation/DocumentTranslator.h"

#include "translation/TranslationCache.h"
#include "translation/TranslationPost.h"

#include <algorithm>

namespace EZTranslator {

TranslatedDocument DocumentTranslator::translate(const QString& assembledText, const QString& source,
                                                 const QString& target, const TranslateFn& fn)
{
    TranslatedDocument document;
    if (assembledText.trimmed().isEmpty())
        return document;

    // Đoạn = tách theo dòng trống; dòng trong đoạn là các dòng đã wrap.
    const QStringList rawParagraphs = assembledText.split(QStringLiteral("\n\n"));

    QStringList units; // text đơn vị để dịch/cache (một dòng)
    for (const QString& raw : rawParagraphs) {
        const QString trimmed = raw.trimmed();
        if (trimmed.isEmpty())
            continue;

        int lines = 0;
        for (const QString& line : trimmed.split(QLatin1Char('\n')))
            if (!line.trimmed().isEmpty())
                ++lines;

        document.sourceParagraphs.append(trimmed);
        document.lineCounts.append(std::max(1, lines));
        units.append(trimmed.simplified());
    }

    if (units.isEmpty())
        return document;

    QStringList translations(units.size());
    QStringList missing;
    QList<int> missingIndex;
    for (int i = 0; i < units.size(); ++i) {
        const QString cached = m_cache ? m_cache->get(units.at(i), source, target) : QString();
        if (!cached.isEmpty())
            translations[i] = cached;
        else {
            missing.append(units.at(i));
            missingIndex.append(i);
        }
    }

    if (!missing.isEmpty() && fn) {
        const QStringList produced = fn(missing);
        for (int k = 0; k < produced.size() && k < missingIndex.size(); ++k) {
            const QString sourceUnit = units.at(missingIndex.at(k));

            // Hậu xử lý: gộp lặp, khôi phục dấu câu nguồn, lọc output rác.
            QString value = TranslationPost::collapseRepeats(produced.at(k));
            value = TranslationPost::applyPunctuation(sourceUnit, value);
            if (value.isEmpty() || !TranslationPost::isUsableTranslation(sourceUnit, value))
                continue; // rác → để trống, lần sau thử lại

            translations[missingIndex.at(k)] = value;
            if (m_cache)
                m_cache->put(sourceUnit, source, target, value);
            ++document.newTranslations;
        }
    }

    document.paragraphs = translations;
    document.complete = std::all_of(translations.cbegin(), translations.cend(),
                                    [](const QString& t) { return !t.isEmpty(); });
    return document;
}

} // namespace EZTranslator
