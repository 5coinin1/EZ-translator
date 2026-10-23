#include "translation/TranslationPost.h"

#include <QSet>
#include <QStringList>

#include <algorithm>

namespace EZTranslator {
namespace TranslationPost {

namespace {

// ── collapseRepeats helpers ──────────────────────────────────────────────────

QStringList wordsOf(const QString& text)
{
    return text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
}

// Dạng so sánh của một từ: chỉ chữ/số, viết thường (dấu câu dính vào từ không che
// được vòng lặp lặp, ví dụ "chà!" ).
QString wordKey(const QString& word)
{
    QString key;
    key.reserve(word.size());
    for (const QChar& c : word)
        if (c.isLetterOrNumber())
            key.append(c.toLower());
    return key;
}

bool sameWord(const QString& a, const QString& b)
{
    const QString ka = wordKey(a);
    return !ka.isEmpty() && ka == wordKey(b);
}

// Xoá một bản sao của mọi cặp n-gram liền nhau giống nhau. true nếu có thay đổi.
bool collapsePass(QStringList& words, int n)
{
    bool changed = false;
    int i = 0;
    while (i + 2 * n <= words.size()) {
        bool same = true;
        for (int k = 0; k < n; ++k) {
            if (!sameWord(words.at(i + k), words.at(i + n + k))) {
                same = false;
                break;
            }
        }
        if (!same) {
            ++i;
            continue;
        }

        if (n == 1) {
            // Từ đơn phải lặp >= 3 lần mới gộp (giữ láy thật "đỏ đỏ").
            int runEnd = i;
            while (runEnd + 1 < words.size() && sameWord(words.at(runEnd + 1), words.at(i)))
                ++runEnd;
            const int runLength = runEnd - i + 1;
            if (runLength < 3) {
                ++i;
                continue;
            }
            words.remove(i + 1, runLength - 1);
            changed = true;
            ++i;
            continue;
        }

        words.remove(i + n, n);
        changed = true;
    }
    return changed;
}

// Phát hiện một đoạn dài bị phát lặp lại (có thể lệch chút) — trim phần lặp.
bool trimRepeatedRun(QStringList& words)
{
    const int size = words.size();
    if (size < 12 || size > 900)
        return false;

    for (int n = size / 2; n >= 5; --n) {
        for (int i = 0; i + 2 * n <= size; ++i) {
            if (!sameWord(words.at(i), words.at(i + n)))
                continue;
            int matches = 0;
            for (int k = 0; k < n; ++k)
                if (sameWord(words.at(i + k), words.at(i + n + k)))
                    ++matches;
            if (matches >= (n * 4) / 5) {
                words.remove(i + n, n);
                return true;
            }
        }
    }
    return false;
}

// ── applyPunctuation helpers ─────────────────────────────────────────────────

bool isLeadMarker(QChar c)
{
    static const QSet<QChar> set = {
        QLatin1Char('-'), QLatin1Char('+'), QLatin1Char('*'),
        QChar(0x2013), QChar(0x2014), QChar(0x2026), QChar(0x00B7)};
    return set.contains(c);
}

bool isTerminal(QChar c)
{
    static const QSet<QChar> set = {QLatin1Char('.'), QLatin1Char('!'), QLatin1Char('?'),
                                    QChar(0x2026), QChar(0x3002), QChar(0xFF01), QChar(0xFF1F),
                                    QChar(0x203C), QChar(0x2049)};
    return set.contains(c);
}

bool isOpeningQuote(QChar c)
{
    static const QSet<QChar> set = {QLatin1Char('"'), QLatin1Char('\''), QChar(0x2018),
                                    QChar(0x201C), QChar(0x300C), QChar(0x300E), QChar(0xFF08),
                                    QLatin1Char('(')};
    return set.contains(c);
}

bool isClosingQuote(QChar c)
{
    static const QSet<QChar> set = {QLatin1Char('"'), QLatin1Char('\''), QChar(0x2019),
                                    QChar(0x201D), QChar(0x300D), QChar(0x300F), QChar(0xFF09),
                                    QLatin1Char(')')};
    return set.contains(c);
}

// Tách phần đầu / thân / dấu kết / dấu đóng của một chuỗi.
void splitAffixes(const QString& text, QString* lead, QString* body, QString* term, QString* close)
{
    QString work = text.trimmed();
    lead->clear();
    term->clear();
    close->clear();

    while (!work.isEmpty() && isClosingQuote(work.at(work.size() - 1))) {
        close->prepend(work.at(work.size() - 1));
        work.chop(1);
    }
    while (!work.isEmpty() && isTerminal(work.at(work.size() - 1))) {
        term->prepend(work.at(work.size() - 1));
        work.chop(1);
    }
    while (!work.isEmpty() && isOpeningQuote(work.at(0))) {
        lead->append(work.at(0));
        work.remove(0, 1);
    }
    if (lead->isEmpty()) {
        QString markers;
        while (!work.isEmpty() && isLeadMarker(work.at(0))) {
            markers.append(work.at(0));
            work.remove(0, 1);
        }
        if (!markers.isEmpty() && !markers.contains(QChar(0x2026)))
            markers += QLatin1Char(' ');
        lead->append(markers);
    }
    *body = work.trimmed();
}

} // namespace

QString collapseRepeats(const QString& text)
{
    QStringList words = wordsOf(text.trimmed());
    if (words.size() < 3)
        return text;

    for (int iteration = 0; iteration < 20; ++iteration) {
        bool changed = false;
        if (trimRepeatedRun(words))
            changed = true;
        for (int n = 1; n <= 4; ++n) {
            if (words.size() < 2 * n)
                continue;
            if (collapsePass(words, n))
                changed = true;
        }
        if (!changed)
            break;
    }

    return words.join(QLatin1Char(' '));
}

QString applyPunctuation(const QString& source, const QString& translation)
{
    const QString trimmed = translation.trimmed();
    if (trimmed.isEmpty())
        return translation;

    QString sourceLead;
    QString sourceBody;
    QString sourceTerm;
    QString sourceClose;
    splitAffixes(source, &sourceLead, &sourceBody, &sourceTerm, &sourceClose);

    if (sourceTerm.isEmpty() && sourceLead.isEmpty() && sourceClose.isEmpty())
        return trimmed;

    QString translationLead;
    QString translationBody;
    QString translationTerm;
    QString translationClose;
    splitAffixes(trimmed, &translationLead, &translationBody, &translationTerm, &translationClose);

    if (translationBody.isEmpty())
        return trimmed;

    QString result = translationBody + sourceTerm;
    if (!sourceLead.isEmpty() || !sourceClose.isEmpty())
        result = sourceLead + result + sourceClose;
    return result;
}

bool isUsableTranslation(const QString& source, const QString& translation)
{
    const QString s = source.trimmed();
    const QString t = translation.trimmed();

    if (t.isEmpty())
        return false;
    if (t.compare(s, Qt::CaseInsensitive) == 0) // echo y hệt nguồn
        return false;

    // Model collapse: một ký tự lặp ("aaaaaa", "......").
    if (t.size() >= 6) {
        const QChar first = t.at(0);
        bool allSame = true;
        for (const QChar c : t) {
            if (c != first) {
                allSame = false;
                break;
            }
        }
        if (allSame)
            return false;
    }

    // Quá ngắn so với nguồn (tiếng Việt thường dài hơn, không thể ngắn bằng 1/6).
    if (s.size() >= 8 && t.size() * 6 < s.size())
        return false;

    // Quá dài bất thường (hallucination), kèm ngưỡng tuyệt đối cho câu nguồn ngắn.
    if (t.size() > std::max(qsizetype(80), qsizetype(s.size() * 6)))
        return false;

    return true;
}

} // namespace TranslationPost
} // namespace EZTranslator
