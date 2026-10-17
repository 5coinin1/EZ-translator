#include "translation/TranslationCache.h"

#include <algorithm>

namespace EZTranslator {

QString TranslationCache::makeKey(const QString& text, const QString& from, const QString& to)
{
    return from + QLatin1Char('\x1f') + to + QLatin1Char('\x1f') + text;
}

QString TranslationCache::get(const QString& text, const QString& from, const QString& to) const
{
    if (text.isEmpty())
        return {};
    return m_map.value(makeKey(text, from, to));
}

void TranslationCache::put(const QString& text, const QString& from, const QString& to,
                           const QString& translation)
{
    if (text.isEmpty() || translation.isEmpty())
        return;

    const QString key = makeKey(text, from, to);
    if (!m_map.contains(key))
        m_order.enqueue(key);
    m_map.insert(key, translation);

    while (m_map.size() > m_limit && !m_order.isEmpty())
        m_map.remove(m_order.dequeue());
}

void TranslationCache::setLimit(int limit)
{
    m_limit = std::max(1, limit);
    while (m_map.size() > m_limit && !m_order.isEmpty())
        m_map.remove(m_order.dequeue());
}

void TranslationCache::clear()
{
    m_map.clear();
    m_order.clear();
}

} // namespace EZTranslator
