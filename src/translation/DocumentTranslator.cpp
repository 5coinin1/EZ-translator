#include "translation/DocumentTranslator.h"

#include "translation/TranslationCache.h"
#include "translation/TranslationPost.h"
#include "translation/ProtectedText.h"

#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace EZTranslator {

QString TranslatedDocument::displayText() const
{
    QStringList visible;
    const int count = std::max(paragraphs.size(), sourceParagraphs.size());
    visible.reserve(count);
    for (int i = 0; i < count; ++i) {
        const QString translated = paragraphs.value(i).trimmed();
        const QString source = sourceParagraphs.value(i).trimmed();
        if (!translated.isEmpty())
            visible.append(translated);
        else if (!source.isEmpty())
            visible.append(source);
    }
    return visible.join(QStringLiteral("\n\n"));
}

namespace {

// Cùng luật với OcrTextAssembler::endsWithSentenceEnd (module translation không link module ocr
// nên giữ bản sao nhỏ này; sửa luật thì sửa cả hai chỗ).
bool endsWithSentenceEnd(const QString& text)
{
    QString trimmed = text.trimmed();
    static const QString closers = QStringLiteral("\"')]}\u201d\u2019\u00bb\u300d\u300f>\u300b\u3011");
    while (!trimmed.isEmpty() && closers.contains(trimmed.back()))
        trimmed.chop(1);
    if (trimmed.isEmpty())
        return false;

    const QChar last = trimmed.back();
    return last == QLatin1Char('.') || last == QLatin1Char('!') || last == QLatin1Char('?')
           || last == QChar(0x2026) || last == QChar(0x3002) || last == QChar(0xFF01)
           || last == QChar(0xFF1F) || last == QChar(0x0964) || last == QChar(0x061F)
           || last == QChar(0x1362) || last == QChar(0x104B) || last == QChar(0x17D4)
           || last == QChar(0x0F0D);
}

bool isVowel(QChar c)
{
    const QChar lower = c.toLower();
    return lower == QLatin1Char('a') || lower == QLatin1Char('e') || lower == QLatin1Char('i')
           || lower == QLatin1Char('o') || lower == QLatin1Char('u') || lower == QLatin1Char('y');
}

bool preserveAcronym(const QString& token)
{
    static const QSet<QString> keep = {
        QStringLiteral("AI"),   QStringLiteral("API"),  QStringLiteral("CPU"), QStringLiteral("CT2"),
        QStringLiteral("CUDA"), QStringLiteral("DPS"),  QStringLiteral("DPI"), QStringLiteral("EU"),
        QStringLiteral("EXP"),  QStringLiteral("FPS"),  QStringLiteral("GDI"), QStringLiteral("GPU"),
        QStringLiteral("HP"),   QStringLiteral("HUD"),  QStringLiteral("ID"),  QStringLiteral("MP"),
        QStringLiteral("NASA"), QStringLiteral("NATO"), QStringLiteral("NPC"), QStringLiteral("OCR"),
        QStringLiteral("OK"),   QStringLiteral("ONNX"), QStringLiteral("PC"),  QStringLiteral("RAM"),
        QStringLiteral("RPG"),  QStringLiteral("TV"),   QStringLiteral("UI"),  QStringLiteral("UK"),
        QStringLiteral("URL"),  QStringLiteral("USA"),  QStringLiteral("USB"), QStringLiteral("UX"),
        QStringLiteral("WGC"),  QStringLiteral("XP"),   QStringLiteral("DLC"), QStringLiteral("MMO"),
    };
    const QString upper = token.toUpper();
    if (keep.contains(upper))
        return true;
    if (token.size() <= 4) {
        bool hasVowel = false;
        for (const QChar& c : token) {
            if (isVowel(c)) {
                hasVowel = true;
                break;
            }
        }
        if (!hasVowel)
            return true;
    }
    return false;
}

/**
 * Chu?n h�a c�ch vi?t hoa tru?c khi d?ch.
 *
 * OCR truy?n tranh/website thu?ng tr? v? c�u VI?T HOA ho?c tr?n case
 * ("WE ARE THe Angels Of Destruction."). CTranslate2/Marian tokenize theo c?
 * ch?, n�n c�u c� nhi?u t? vi?t hoa cho ra b?n d?ch sai nghia. H�m n�y ch? h?
 * c�c t? VI?T HOA (tr? nh�n ng?n nhu NPC/HP/EXP/USA...) r?i vi?t hoa ch? c�i
 * d?u c�u. Text ngu?n hi?n th? v?n gi? nguy�n.
 */
QString normalizeAllCapsForTranslation(const QString& text)
{
    struct Span
    {
        int start;
        int end;
    };
    QList<Span> spans;
    for (int i = 0; i < text.size();) {
        if (!text.at(i).isLetter()) {
            ++i;
            continue;
        }
        const int start = i;
        while (i < text.size()
               && (text.at(i).isLetter() || text.at(i) == QLatin1Char('\'')
                   || text.at(i) == QChar(0x2019))) {
            ++i;
        }
        spans.append({start, i});
    }

    int allCapsWords = 0;
    int longCapsWords = 0;
    for (const Span& span : spans) {
        int letters = 0;
        bool allUpper = true;
        for (int i = span.start; i < span.end; ++i) {
            if (text.at(i).isLetter()) {
                ++letters;
                if (!text.at(i).isUpper()) {
                    allUpper = false;
                    break;
                }
            }
        }
        // B? qua ch? m?t k� t? (A, B, I...) d? kh�ng k�o c?u thu?ng v�o ch? d? chu?n h�a.
        if (letters >= 2 && allUpper) {
            ++allCapsWords;
            if (span.end - span.start >= 4)
                ++longCapsWords;
        }
    }

    // Kh�ng d?ng v�o c�u thu?ng ch? c� m?t nh�n ng?n ("I love NASA.").
    if (longCapsWords == 0 && allCapsWords < 2)
        return text;

    QString out = text;
    for (const Span& span : spans) {
        bool hasLetter = false;
        bool allUpper = true;
        for (int i = span.start; i < span.end; ++i) {
            if (text.at(i).isLetter()) {
                hasLetter = true;
                if (!text.at(i).isUpper()) {
                    allUpper = false;
                    break;
                }
            }
        }
        if (!hasLetter || !allUpper)
            continue;
        const QString token = text.mid(span.start, span.end - span.start);
        if (preserveAcronym(token))
            continue;
        for (int i = span.start; i < span.end; ++i) {
            if (out.at(i).isLetter())
                out[i] = out.at(i).toLower();
        }
    }

    for (int i = 0; i < out.size(); ++i) {
        if (out.at(i).isLetter()) {
            out[i] = out.at(i).toUpper();
            break;
        }
    }

    // Gi? "I" v� c�c d?ng "I'm", "I've", "I'll", "I'd" ? ch? vi?t hoa.
    static const QRegularExpression standaloneI(QStringLiteral("\\bi\\b"));
    out.replace(standaloneI, QStringLiteral("I"));
    static const QRegularExpression iContraction(
        QStringLiteral("\\bi(['\\x{2019}])(m|ve|ll|d)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    out.replace(iContraction, QStringLiteral("I\\1\\2"));

    return out;
}

// Đơn vị dịch tối đa ~140 ký tự. Thời gian decode tỉ lệ với đơn vị DÀI NHẤT trong lô (cả lô chờ
// nó): một đoạn 300-400 ký tự làm mọi lượt gọi ~130ms+ dù các đoạn khác ngắn, còn cắt về 140 thì
// lượt gọi về ~100ms và cache cũng mịn hơn (cuộn lại chỉ tốn phần thật sự mới). Cắt ưu tiên ranh
// giới MỆNH ĐỀ (`,` `;` `:` gạch) rồi mới tới ranh giới từ — mảnh ghép lại vẫn đọc như mệnh đề.
constexpr int kMaxUnitChars = 140;

// Tách đoạn thành các CÂU (đơn vị dịch) — port từ bản translator (OcrTextAssembler::splitSentences,
// group=false). ĐÂY là điểm quyết định CACHE DỊCH có trúng khi cuộn hay không:
//   * đơn vị = CÂU  → cuộn 1 dòng chỉ đổi 1-2 câu, các câu còn lại trúng cache (bản translator);
//   * cắt cả đoạn thành khúc 140 ký tự → vị trí cắt trôi theo nội dung, MỌI khúc đổi text ⇒ trượt
//     100% ⇒ dịch lại cả trang mỗi lần cuộn (log thật EZ: new= 12/12, "dịch" ~1000ms/lần).
// Câu quá dài (không có dấu câu) mới bị cắt nhỏ, ưu tiên ranh giới MỆNH ĐỀ như trước.
QStringList splitSentences(const QString& text, int maxChars)
{
    if (maxChars < 32)
        maxChars = 32;

    const auto isSentenceEnd = [](QChar c) {
        return c == QLatin1Char('.') || c == QLatin1Char('!') || c == QLatin1Char('?')
               || c == QChar(0x2026) /* … */ || c == QChar(0x3002) /* 。 */
               || c == QChar(0xFF01) /* ！ */ || c == QChar(0xFF1F) /* ？ */
               || c == QChar(0x0964) /* । */ || c == QChar(0x061F) /* ؟ */
               || c == QChar(0x1362) /* ። */ || c == QChar(0x104B) /* ။ */
               || c == QChar(0x17D4) /* ។ */ || c == QChar(0x0F0D) /* ། */;
    };
    // Dấu ĐÓNG wrapper cũng kết thúc đơn vị như dấu chấm: dòng tiêu đề "<<...>>" đứng riêng.
    const auto isWrapClose = [](QChar c) {
        return c == QLatin1Char('>') || c == QChar(0x300b) || c == QChar(0x3011);
    };
    const auto isCloser = [](QChar c) {
        static const QString closers = QStringLiteral("\"')]}\u201d\u2019\u00bb\u300d\u300f");
        return closers.contains(c);
    };

    QStringList sentences;
    QString current;
    // Độ sâu wrapper: dấu chấm Ở TRONG cặp chưa đóng không kết thúc đơn vị ("<<It was summer.>>").
    int wrapDepth = 0;
    for (int index = 0; index < text.size(); ++index) {
        const QChar ch = text.at(index);
        if (ch == QLatin1Char('<')) {
            ++wrapDepth;
        } else if (ch == QLatin1Char('>')) {
            wrapDepth = std::max(0, wrapDepth - 1);
        } else if (QStringLiteral("\u300a\u3010").contains(ch)) {
            ++wrapDepth;
        } else if (QStringLiteral("\u300b\u3011").contains(ch)) {
            wrapDepth = std::max(0, wrapDepth - 1);
        }

        current.append(ch);
        const bool closesWrap = isWrapClose(ch) && wrapDepth == 0 && current.trimmed().size() > 1;
        if ((!isSentenceEnd(ch) && !closesWrap) || wrapDepth > 0)
            continue;

        // Chuỗi dấu kết câu liền nhau là MỘT đơn vị ("...", "!!", "?!") — tách từng ký tự làm
        // '"..."' vỡ thành quote trần + dấu chấm, model trả về vòng lặp rác.
        int next = index + 1;
        while (next < text.size() && isSentenceEnd(text.at(next))) {
            current.append(text.at(next));
            ++next;
        }
        // Dấu đóng ngoặc kép thuộc về câu vừa kết thúc.
        while (next < text.size() && isCloser(text.at(next))) {
            current.append(text.at(next));
            ++next;
        }

        const QString trimmed = current.trimmed();
        if (!trimmed.isEmpty())
            sentences.append(trimmed);
        current.clear();
        index = next - 1;
    }
    const QString tail = current.trimmed();
    if (!tail.isEmpty())
        sentences.append(tail);

    // Chỉ câu QUÁ DÀI mới bị cắt nhỏ (câu không có dấu câu) — giữ thuật toán cũ.
    QStringList out;
    for (const QString& unit : sentences) {
        if (unit.size() <= maxChars) {
            out.append(unit);
            continue;
        }
        const QStringList words = unit.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (words.size() <= 1) {
            for (int i = 0; i < unit.size(); i += maxChars)
                out.append(unit.mid(i, maxChars));
            continue;
        }
        QString rest = unit;
        while (rest.size() > maxChars) {
            int cut = -1;
            const int from = std::min<int>(maxChars - 1, rest.size() - 1);
            for (int i = from; i >= maxChars / 3; --i) {
                const QChar ch = rest.at(i);
                if (ch == QLatin1Char(',') || ch == QLatin1Char(';') || ch == QLatin1Char(':')
                    || ch == QChar(0x2014) /* em dash */ || ch == QChar(0x2013) /* en dash */) {
                    cut = i + 1;
                    break;
                }
            }
            if (cut < 0) {
                const int space = rest.lastIndexOf(QLatin1Char(' '), maxChars - 1);
                if (space >= maxChars / 3)
                    cut = space + 1;
            }
            if (cut < 0)
                cut = maxChars;
            out.append(rest.left(cut).trimmed());
            rest = rest.mid(cut).trimmed();
        }
        if (!rest.isEmpty())
            out.append(rest);
    }
    return out;
}

} // namespace

TranslatedDocument DocumentTranslator::translate(const QString& assembledText, const QString& source,
                                                 const QString& target, const TranslateFn& fn)
{
    TranslatedDocument document;
    if (assembledText.trimmed().isEmpty())
        return document;

    // Đoạn = tách theo DÒNG, không chỉ theo dòng trống: một dòng kết thúc bằng DẤU CÂU (. ! ? …)
    // cũng ĐÓNG đoạn — luật của bản translator: dấu "." GIỮA dòng không ngắt, dấu "." CUỐI dòng thì
    // ngắt. Chỉ tách theo dòng trống làm trang không có dòng trống dính thành MỘT đoạn, bản dịch ra
    // một khối chữ liền (đúng hiện tượng "bản dịch dính vào nhau").
    QList<QStringList> paragraphLines;
    QStringList currentLines;
    const auto flushParagraph = [&]() {
        if (!currentLines.isEmpty()) {
            paragraphLines.append(currentLines);
            currentLines.clear();
        }
    };
    for (const QString& rawLine : assembledText.split(QLatin1Char('\n'))) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty()) {
            flushParagraph();
            continue;
        }
        if (isProtectedCode(line) || isPageNumber(line)) {
            flushParagraph();
            paragraphLines.append({line});
            continue;
        }
        currentLines.append(line);
        if (endsWithSentenceEnd(line))
            flushParagraph();
    }
    flushParagraph();

    QStringList units;        // đơn vị dịch (mỗi CÂU một đơn vị — xem splitSentences)
    QList<int> unitParagraph; // mỗi đơn vị thuộc đoạn nào
    for (const QStringList& lines : paragraphLines) {
        if (lines.isEmpty())
            continue;

        const int paragraphIndex = document.sourceParagraphs.size();
        document.sourceParagraphs.append(lines.join(QLatin1Char('\n')));
        document.lineCounts.append(std::max(1, int(lines.size())));
        if (lines.size() == 1
            && (isProtectedCode(lines.first()) || isPageNumber(lines.first())))
            continue;
        for (const QString& unit :
             splitSentences(lines.join(QLatin1Char(' ')).simplified(), kMaxUnitChars)) {
            units.append(unit);
            unitParagraph.append(paragraphIndex);
        }
    }

    // Engine nhận bản dạng câu cho câu VIẾT HOA TOÀN BỘ; text nguồn vẫn giữ nguyên để
    // hiển thị/so khớp overlay. Cache dùng khóa đã chuẩn hóa để tái dùng bản dịch câu thường.
    QStringList unitsForTranslation;
    unitsForTranslation.reserve(units.size());
    for (const QString& unit : units)
        unitsForTranslation.append(normalizeAllCapsForTranslation(unit));

    document.paragraphs = QStringList(document.sourceParagraphs.size());
    for (int i = 0; i < document.sourceParagraphs.size(); ++i) {
        const QString& paragraph = document.sourceParagraphs.at(i);
        if (isProtectedCode(paragraph) || isPageNumber(paragraph))
            document.paragraphs[i] = paragraph;
    }
    if (units.isEmpty()) {
        document.complete = true;
        return document;
    }

    QStringList translations(units.size());
    QStringList missing;
    QList<int> missingIndex;
    for (int i = 0; i < units.size(); ++i) {
        const QString& unitForTranslation = unitsForTranslation.at(i);
        const QString cached =
            m_cache ? m_cache->get(unitForTranslation, source, target) : QString();
        if (!cached.isEmpty())
            translations[i] = cached;
        else {
            missing.append(unitForTranslation);
            missingIndex.append(i);
        }
    }

    if (!missing.isEmpty() && fn) {
        const QStringList produced = fn(missing);
        for (int k = 0; k < produced.size() && k < missingIndex.size(); ++k) {
            const int unitIndex = missingIndex.at(k);
            const QString sourceUnit = units.at(unitIndex);
            const QString& unitForTranslation = unitsForTranslation.at(unitIndex);

            // Hậu xử lý: gộp lặp, khôi phục dấu câu nguồn, lọc output rác.
            QString value = TranslationPost::collapseRepeats(produced.at(k));
            value = TranslationPost::applyPunctuation(sourceUnit, value);
            value = TranslationPost::normalizePunctuation(value, target);
            if (value.isEmpty() || !TranslationPost::isUsableTranslation(sourceUnit, value))
                continue; // rác → để trống, lần sau thử lại

            translations[unitIndex] = value;
            if (m_cache)
                m_cache->put(unitForTranslation, source, target, value);
            ++document.newTranslations;
        }
    }

    // Ghép các đơn vị đã dịch lại theo đoạn (thứ tự giữ nguyên như nguồn).
    for (int i = 0; i < units.size(); ++i) {
        const QString& value = translations.at(i);
        if (value.isEmpty())
            continue;
        QString& paragraph = document.paragraphs[unitParagraph.at(i)];
        paragraph = paragraph.isEmpty() ? value : paragraph + QLatin1Char(' ') + value;
    }

    document.complete = std::all_of(document.paragraphs.cbegin(), document.paragraphs.cend(),
                                    [](const QString& t) { return !t.isEmpty(); });
    return document;
}

} // namespace EZTranslator
