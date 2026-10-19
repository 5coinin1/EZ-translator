#include "overlay/ParagraphAnchor.h"

#include <algorithm>

namespace ParagraphAnchor {

namespace {

// How many leading words of a paragraph are used for the match (enough to be specific, few enough
// to survive OCR noise at the start of a line).
constexpr int kWords = 6;
// How far ahead of the previous paragraph's end a paragraph may start (a merged/dropped line),
// and how far BACK it may look (a document with extra lines drifts paragraphs downwards).
// Cửa sổ hẹp: khớp nhầm trong cửa sổ rộng chính là "thi thoảng đặt text sai vị trí" — trôi thật
// cần bắt chỉ 1-2 dòng, còn ±8 dòng cho quá nhiều cơ hội khớp vào câu na ná ở xa.
constexpr int kWindow = 5;
constexpr int kBack = 3;

QStringList leadingWords(const QString &paragraph)
{
    const QStringList words = paragraph.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QStringList out;
    for (int i = 0; i < words.size() && i < kWords; ++i) {
        QString word = words.at(i);
        // Punctuation at the edges would make the containment test fail ("end." vs "end").
        while (!word.isEmpty() && !word.front().isLetterOrNumber())
            word.remove(0, 1);
        while (!word.isEmpty() && !word.back().isLetterOrNumber())
            word.chop(1);
        if (word.size() >= 2)
            out.append(word);
    }
    return out;
}

double scoreLine(const QString &line, const QStringList &words)
{
    if (words.isEmpty())
        return 0.0;
    int hits = 0;
    for (const QString &word : words) {
        if (line.contains(word, Qt::CaseInsensitive))
            ++hits;
    }
    return double(hits) / double(words.size());
}

} // namespace

QList<int> anchor(const QStringList &paragraphTexts, const QStringList &sourceLines,
                  const QList<int> &lineCounts)
{
    QList<int> starts;
    starts.reserve(paragraphTexts.size());
    int cursor = 0;
    for (int p = 0; p < paragraphTexts.size(); ++p) {
        const int count = std::max(1, lineCounts.value(p, 1));
        int best = std::min(cursor, std::max(0, int(sourceLines.size()) - 1));
        const QStringList words = leadingWords(paragraphTexts.at(p));
        if (!words.isEmpty() && !sourceLines.isEmpty()) {
            double bestScore = 0.5; // dưới 50% số từ khớp thì coi là NHIỄU: giữ vị trí theo chỉ số
            const int from = std::max(0, cursor - kBack);
            const int limit = std::min(int(sourceLines.size()), cursor + kWindow);
            for (int line = from; line < limit; ++line) {
                const double score = scoreLine(sourceLines.at(line), words);
                if (score > bestScore) {
                    bestScore = score;
                    best = line;
                }
            }
        }
        starts.append(best);
        cursor = std::min(int(sourceLines.size()), best + count);
    }
    return starts;
}

} // namespace ParagraphAnchor
