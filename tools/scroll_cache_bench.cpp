// Đo ĐÚNG cảnh người dùng: "cuộn 1 dòng mà dịch như trang mới".
// So sánh hai cách chia ĐƠN VỊ DỊCH trên cùng engine thật (ct2 int8):
//   * MỚI  — đơn vị = CÂU (DocumentTranslator hiện tại, kiến trúc translator);
//   * CŨ   — đơn vị = CẮT ĐOẠN thành khúc ≤140 ký tự tại dấu mệnh đề (bản sao thuật toán cũ,
//            chỉ để đo — cách này làm vị trí cắt trôi theo nội dung nên cuộn 1 dòng là trượt hết).
//
//   scroll_cache_bench.exe
// Kết quả ghi ra scroll_cache_bench.out.txt (console môi trường này không hiện qInfo).
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QTextStream>

#include <algorithm>

#include "translation/CTranslate2Translator.h"
#include "translation/DocumentTranslator.h"
#include "translation/TranslationCache.h"

using namespace EZTranslator;

namespace {

// ── Bản sao thuật toán CŨ (cắt đoạn thành khúc ≤140 ký tự, ưu tiên dấu mệnh đề) — chỉ để đo ──
constexpr int kMaxUnitChars = 140;

QStringList oldStyleUnits(const QString& text)
{
    QStringList out;
    QString rest = text;
    while (rest.size() > kMaxUnitChars) {
        int cut = -1;
        for (int i = std::min<int>(kMaxUnitChars - 1, rest.size() - 1); i >= kMaxUnitChars / 3;
             --i) {
            const QChar ch = rest.at(i);
            if (ch == QLatin1Char(',') || ch == QLatin1Char(';') || ch == QLatin1Char(':')
                || ch == QChar(0x2014) || ch == QChar(0x2013)) {
                cut = i + 1;
                break;
            }
        }
        if (cut < 0) {
            const int space = rest.lastIndexOf(QLatin1Char(' '), kMaxUnitChars - 1);
            if (space >= kMaxUnitChars / 3)
                cut = space + 1;
        }
        if (cut < 0)
            cut = kMaxUnitChars;
        out.append(rest.left(cut).trimmed());
        rest = rest.mid(cut).trimmed();
    }
    if (!rest.isEmpty())
        out.append(rest);
    return out;
}

// Một "trang" 14 câu, ngắt dòng GIỮA câu (không dòng nào kết thúc bằng dấu chấm → cả trang là
// MỘT đoạn — đúng dạng trang game/visual novel mà người dùng đang dịch).
QString makePage(int firstSentence)
{
    QStringList sentences;
    for (int i = firstSentence; i < firstSentence + 14; ++i)
        sentences.append(QStringLiteral("Sentence number %1 talks about something interesting "
                                        "and keeps going for a while")
                             .arg(i));
    const QString text = sentences.join(QStringLiteral(". ")) + QLatin1Char('.');
    QStringList wrapped;
    constexpr int kLineChars = 48;
    for (int i = 0; i < text.size(); i += kLineChars)
        wrapped.append(text.mid(i, kLineChars));
    return wrapped.join(QLatin1Char('\n'));
}

// Cuộn 1 dòng: bỏ dòng đầu, thêm một câu mới ở cuối.
QString scrollOneLine(const QString& page, int newSentence)
{
    const int firstNewline = page.indexOf(QLatin1Char('\n'));
    const QString rest = firstNewline >= 0 ? page.mid(firstNewline + 1) : page;
    return rest + QLatin1Char('\n')
           + QStringLiteral("Sentence number %1 is brand new here").arg(newSentence);
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QFile::remove(QStringLiteral("scroll_cache_bench.out.txt"));
    QFile report(QStringLiteral("scroll_cache_bench.out.txt"));
    report.open(QIODevice::WriteOnly | QIODevice::Append);
    QTextStream out(&report);

    const QString src = QStringLiteral("en");
    const QString tgt = QStringLiteral("vi");
    const QString dir =
        qEnvironmentVariable("EZ_MODELS_DIR", QStringLiteral(EZ_DEFAULT_MODELS_DIR));

    CTranslate2Translator ct2;
    QString error;
    const QString python = CTranslate2Translator::findPython();
    const bool ready = !python.isEmpty()
                       && ct2.start(python, QString(), dir + QStringLiteral("/ct2-en-vi-int8"),
                                    QStringLiteral("int8"), src, tgt, &error);
    out << "ct2 ready = " << (ready ? 1 : 0) << " | " << error << "\n";
    if (!ready) {
        out.flush();
        return 1;
    }

    const auto engine = [&ct2, &src, &tgt](const QStringList& units) {
        return ct2.translate(units, src, tgt);
    };

    // ── Cách MỚI: DocumentTranslator (đơn vị = câu) ─────────────────────────
    TranslationCache cache;
    DocumentTranslator translator(&cache);

    const QString pageA = makePage(1);
    QElapsedTimer timer;
    timer.start();
    const TranslatedDocument docA = translator.translate(pageA, src, tgt, engine);
    out << "[MỚI] trang A (lạnh):     " << timer.elapsed() << " ms | new="
        << docA.newTranslations << "/" << docA.paragraphs.size() << " đoạn\n";

    const QString pageB = scrollOneLine(pageA, 100);
    timer.restart();
    const TranslatedDocument docB = translator.translate(pageB, src, tgt, engine);
    out << "[MỚI] cuộn 1 dòng → B:    " << timer.elapsed() << " ms | new="
        << docB.newTranslations << "\n";

    const QString pageC = scrollOneLine(pageB, 101);
    timer.restart();
    const TranslatedDocument docC = translator.translate(pageC, src, tgt, engine);
    out << "[MỚI] cuộn 1 dòng → C:    " << timer.elapsed() << " ms | new="
        << docC.newTranslations << "\n";

    // Đường "hiện ngay" của emit#1: chỉ tra cache, KHÔNG gọi engine.
    timer.restart();
    const TranslatedDocument cachedC = translator.translate(pageC, src, tgt, nullptr);
    int cachedParagraphs = 0;
    for (const QString& paragraph : cachedC.paragraphs) {
        if (!paragraph.isEmpty())
            ++cachedParagraphs;
    }
    out << "[MỚI] emit#1 (cache-only) cho C: " << timer.elapsed() << " ms | đoạn hiện được ngay = "
        << cachedParagraphs << "/" << cachedC.paragraphs.size() << "\n";

    // ── Cách CŨ: cắt khúc 140 ký tự (mô phỏng lại để đo) ────────────────────
    QHash<QString, QString> oldCache;
    const auto oldTranslate = [&](const QString& page) {
        const QString paragraph = QStringList(page.split(QLatin1Char('\n'))).join(QLatin1Char(' '))
                                      .simplified();
        const QStringList units = oldStyleUnits(paragraph);
        QStringList missing;
        for (const QString& unit : units) {
            if (!oldCache.contains(unit))
                missing.append(unit);
        }
        if (!missing.isEmpty()) {
            const QStringList produced = engine(missing);
            for (int i = 0; i < produced.size() && i < missing.size(); ++i)
                oldCache.insert(missing.at(i), produced.at(i));
        }
        return units.size() - missing.size(); // số đơn vị trúng cache
    };

    timer.restart();
    const int hitsA = oldTranslate(pageA);
    out << "[CŨ]  trang A (lạnh):     " << timer.elapsed() << " ms | trúng cache=" << hitsA
        << "\n";
    timer.restart();
    const int hitsB = oldTranslate(pageB);
    out << "[CŨ]  cuộn 1 dòng → B:    " << timer.elapsed() << " ms | trúng cache=" << hitsB
        << " | tổng đơn vị=" << oldStyleUnits(pageB).size() << "\n";
    timer.restart();
    const int hitsC = oldTranslate(pageC);
    out << "[CŨ]  cuộn 1 dòng → C:    " << timer.elapsed() << " ms | trúng cache=" << hitsC
        << " | tổng đơn vị=" << oldStyleUnits(pageC).size() << "\n";

    out.flush();
    return 0;
}
