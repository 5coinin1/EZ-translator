#include "translation/TranslationWorker.h"

#include "translation/GoogleTranslator.h"
#include "translation/MyMemoryTranslator.h"

namespace EZTranslator {

TranslationWorker::TranslationWorker(QObject* parent)
    : QObject(parent)
{
}

TranslationWorker::~TranslationWorker() = default;

void TranslationWorker::configure(const QString& source, const QString& target)
{
    m_source = source.isEmpty() ? QStringLiteral("auto") : source;
    m_target = target.isEmpty() ? QStringLiteral("vi") : target;

    if (!m_primary)
        m_primary = std::make_unique<GoogleTranslator>();
    if (!m_fallback)
        m_fallback = std::make_unique<MyMemoryTranslator>();
}

QStringList TranslationWorker::runEngines(const QStringList& units)
{
    QStringList out = m_primary ? m_primary->translate(units, m_source, m_target) : QStringList();
    while (out.size() < units.size())
        out.append(QString());

    // Engine phụ cho unit mà engine chính bỏ trống (bị chặn/giới hạn).
    QStringList missing;
    QList<int> missingIndex;
    for (int i = 0; i < units.size(); ++i) {
        if (out.at(i).isEmpty() && !units.at(i).trimmed().isEmpty()) {
            missing.append(units.at(i));
            missingIndex.append(i);
        }
    }
    if (!missing.isEmpty() && m_fallback) {
        const QStringList alt = m_fallback->translate(missing, m_source, m_target);
        for (int k = 0; k < alt.size() && k < missingIndex.size(); ++k) {
            if (!alt.at(k).isEmpty())
                out[missingIndex.at(k)] = alt.at(k);
        }
    }
    return out;
}

void TranslationWorker::translate(const QString& id, const QString& text, quint64 token)
{
    if (!m_primary)
        configure(m_source, m_target);

    DocumentTranslator translator(&m_cache);
    const TranslatedDocument document =
        translator.translate(text, m_source, m_target,
                             [this](const QStringList& units) { return runEngines(units); });

    emit translated(id, token, document);
}

} // namespace EZTranslator
