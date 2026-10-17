/**
 * @file test_translation.cpp
 * @brief Unit tests cho module translation/ (cache + ghép đoạn).
 *
 *   T1 – TranslationCache: put/get/size/clear
 *   T2 – DocumentTranslator: tách đoạn + đếm dòng nguồn
 *   T3 – DocumentTranslator: cache → lần 2 không gọi engine
 *   T4 – DocumentTranslator: unit lỗi → paragraphs rỗng, complete=false
 */

#include <QtTest/QtTest>

#include "translation/DocumentTranslator.h"
#include "translation/LanguageDetect.h"
#include "translation/TranslationCache.h"
#include "translation/TranslationPost.h"

using namespace EZTranslator;

class TestTranslation : public QObject
{
    Q_OBJECT

private slots:

    // ── T1 ───────────────────────────────────────────────────────────────────
    void cache_basics()
    {
        TranslationCache cache(2);
        QVERIFY(cache.get("hello", "en", "vi").isEmpty());

        cache.put("hello", "en", "vi", "xin chào");
        QCOMPARE(cache.get("hello", "en", "vi"), QString("xin chào"));
        QCOMPARE(cache.get("hello", "en", "fr"), QString()); // khác ngôn ngữ đích

        cache.clear();
        QCOMPARE(cache.size(), 0);
    }

    // ── T2 ───────────────────────────────────────────────────────────────────
    void document_splitsParagraphs()
    {
        TranslationCache cache;
        DocumentTranslator translator(&cache);

        const QString text = QStringLiteral("Line A\nLine B\n\nPara 2 line 1\nPara 2 line 2");
        const TranslatedDocument doc = translator.translate(
            text, "en", "vi", [](const QStringList& units) {
                QStringList out;
                for (const QString& unit : units)
                    out.append(QStringLiteral("BAN ") + unit);
                return out;
            });

        QCOMPARE(doc.sourceParagraphs.size(), 2);
        QCOMPARE(doc.lineCounts.size(), 2);
        QCOMPARE(doc.lineCounts.at(0), 2);
        QCOMPARE(doc.lineCounts.at(1), 2);
        QCOMPARE(doc.sourceParagraphs.at(0), QString("Line A\nLine B"));
        QCOMPARE(doc.paragraphs.at(0), QString("BAN Line A Line B"));
        QVERIFY(doc.complete);
        QCOMPARE(doc.newTranslations, 2);
    }

    // ── T3 ───────────────────────────────────────────────────────────────────
    void document_usesCache()
    {
        TranslationCache cache;
        DocumentTranslator translator(&cache);
        const QString text = QStringLiteral("Hello world");

        int calls = 0;
        const auto fn = [&calls](const QStringList& units) {
            ++calls;
            QStringList out;
            for (const QString& unit : units)
                out.append(QStringLiteral("vi:") + unit);
            return out;
        };

        const TranslatedDocument first = translator.translate(text, "en", "vi", fn);
        QCOMPARE(first.newTranslations, 1);
        QCOMPARE(calls, 1);

        const TranslatedDocument second = translator.translate(text, "en", "vi", fn);
        QCOMPARE(second.newTranslations, 0); // lấy từ cache
        QCOMPARE(calls, 1);                  // không gọi engine lần nữa
        QVERIFY(second.complete);
    }

    // ── T4 ───────────────────────────────────────────────────────────────────
    void document_handlesFailure()
    {
        TranslationCache cache;
        DocumentTranslator translator(&cache);

        const TranslatedDocument doc = translator.translate(
            QStringLiteral("Some text"), "en", "vi",
            [](const QStringList& units) { return QStringList{ QString() }; });

        QCOMPARE(doc.paragraphs.size(), 1);
        QVERIFY(doc.paragraphs.at(0).isEmpty());
        QVERIFY(!doc.complete);
        QCOMPARE(doc.newTranslations, 0);
    }

    // ── T5: collapse lặp (giữ láy thật) ──────────────────────────────────────
    void post_collapseRepeats()
    {
        QCOMPARE(TranslationPost::collapseRepeats(QStringLiteral("đau đau đau đau đớn")),
                 QStringLiteral("đau đớn"));
        QCOMPARE(TranslationPost::collapseRepeats(QStringLiteral("đỏ đỏ")),
                 QStringLiteral("đỏ đỏ")); // láy thật giữ nguyên
    }

    // ── T6: giữ dấu câu/đánh dấu hội thoại của nguồn ─────────────────────────
    void post_applyPunctuation()
    {
        QCOMPARE(TranslationPost::applyPunctuation(QStringLiteral("- Hello!"),
                                                   QStringLiteral("Xin chào")),
                 QStringLiteral("- Xin chào!"));
        QCOMPARE(TranslationPost::applyPunctuation(QStringLiteral("Hello"),
                                                   QStringLiteral("Xin chào")),
                 QStringLiteral("Xin chào"));
    }

    // ── T7: lọc bản dịch rác ─────────────────────────────────────────────────
    void post_isUsable()
    {
        QVERIFY(!TranslationPost::isUsableTranslation(QStringLiteral("Hello world"),
                                                      QStringLiteral("Hello world"))); // echo
        QVERIFY(!TranslationPost::isUsableTranslation(QStringLiteral("Hello"),
                                                      QStringLiteral("aaaaaa"))); // collapse
        QVERIFY(!TranslationPost::isUsableTranslation(QStringLiteral("This is a long sentence"),
                                                      QStringLiteral("x"))); // quá ngắn
        QVERIFY(!TranslationPost::isUsableTranslation(QStringLiteral("Hi"), QString(200, 'a')));
        QVERIFY(TranslationPost::isUsableTranslation(QStringLiteral("Hello world"),
                                                     QStringLiteral("Xin chào thế giới")));
    }

    // ── T8: nhận diện ngôn ngữ theo hệ chữ ───────────────────────────────────
    void languageDetect_scripts()
    {
        QVERIFY(LanguageDetect::isAuto(QString()));
        QVERIFY(LanguageDetect::isAuto(QStringLiteral("auto")));
        QVERIFY(!LanguageDetect::isAuto(QStringLiteral("en")));

        QCOMPARE(LanguageDetect::detect(QStringLiteral("Hello world")), QStringLiteral("en"));
        QCOMPARE(LanguageDetect::detect(QStringLiteral("你好世界")), QStringLiteral("zh"));
        QCOMPARE(LanguageDetect::detect(QStringLiteral("こんにちは")), QStringLiteral("ja"));
        QCOMPARE(LanguageDetect::detect(QStringLiteral("привет")), QStringLiteral("ru"));
        QCOMPARE(LanguageDetect::detect(QStringLiteral("你好世界"), {QStringLiteral("en")}),
                 QString());
        QCOMPARE(LanguageDetect::detect(QStringLiteral("123 !!!")), QString());
    }
};

QTEST_MAIN(TestTranslation)
#include "test_translation.moc"
