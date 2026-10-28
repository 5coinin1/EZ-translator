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

    void popup_keepsEveryTranslatedParagraph()
    {
        TranslatedDocument document;
        document.sourceParagraphs = {QStringLiteral("First."), QStringLiteral("Second."),
                                     QStringLiteral("Third.")};
        document.paragraphs = {QStringLiteral("Thứ nhất."), QStringLiteral("Thứ hai."),
                               QStringLiteral("Thứ ba.")};
        QCOMPARE(document.displayText(), QStringLiteral("Thứ nhất.\n\nThứ hai.\n\nThứ ba."));

        document.paragraphs[1].clear();
        QCOMPARE(document.displayText(), QStringLiteral("Thứ nhất.\n\nSecond.\n\nThứ ba."));
    }

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

    // ── T5 ──────────────────────────────────────────────────────────────────
    void document_capsLongUnits()
    {
        // Đoạn dài phải được cắt thành nhiều đơn vị ≤ ~140 ký tự trước khi gửi engine (thời gian
        // decode tỉ lệ với đơn vị DÀI NHẤT trong lô), rồi ghép lại theo đoạn.
        TranslationCache cache;
        DocumentTranslator translator(&cache);
        const QString longParagraph =
            QStringLiteral("The first sentence is quite long and keeps going for a while, ")
            + QStringLiteral("then a second sentence continues the story with even more words, ")
            + QStringLiteral("and a third one closes the paragraph for good.");

        QStringList received;
        const TranslatedDocument doc =
            translator.translate(longParagraph, "en", "vi", [&](const QStringList& units) {
                received = units;
                QStringList out;
                for (const QString& unit : units)
                    out.append(QStringLiteral("VI:") + unit);
                return out;
            });

        QVERIFY2(received.size() >= 2, "đoạn dài phải bị cắt thành nhiều đơn vị");
        for (const QString& unit : received)
            QVERIFY2(unit.size() <= 150, "đơn vị gửi engine phải ≤ ~140 ký tự");
        QCOMPARE(doc.paragraphs.size(), 1);
        QVERIFY(doc.paragraphs.at(0).contains(QStringLiteral("VI:")));
        QVERIFY(doc.complete);
    }

    // ── T6 ──────────────────────────────────────────────────────────────────
    void document_splitsParagraphsAtSentenceEnds()
    {
        // KHÔNG có dòng trống, nhưng các dòng KẾT THÚC bằng dấu câu → mỗi cụm là một đoạn riêng
        // (luật của bản translator: dấu "." cuối dòng thì ngắt, giữa dòng thì không). Trước đây
        // chỉ tách theo dòng trống nên cả trang dính thành một đoạn, bản dịch ra một khối liền.
        TranslationCache cache;
        DocumentTranslator translator(&cache);
        const QString text = QStringLiteral("First sentence ends here.\n"
                                            "Second sentence ends here too.\n"
                                            "Third one is still going and does not end\n"
                                            "until this line finishes it.");
        const TranslatedDocument doc = translator.translate(
            text, "en", "vi", [](const QStringList& units) {
                QStringList out;
                for (const QString& unit : units)
                    out.append(QStringLiteral("VI:") + unit);
                return out;
            });

        QCOMPARE(doc.sourceParagraphs.size(), 3);
        QCOMPARE(doc.lineCounts.at(0), 1);
        QCOMPARE(doc.lineCounts.at(1), 1);
        QCOMPARE(doc.lineCounts.at(2), 2);
        QCOMPARE(doc.paragraphs.size(), 3);
    }

    // ── T7: CUỘN 1 DÒNG → gần hết đơn vị phải TRÚNG CACHE ─────────────────────
    void document_scrollKeepsCacheHits()
    {
        // Đúng lỗi người dùng báo ("cuộn 1 dòng mà dịch như trang mới", log thật new= 12/12,
        // "dịch" ~1000ms/lần): đơn vị dịch từng bị CẮT theo vị trí (khúc 140 ký tự) nên chỉ cần
        // nội dung trôi một dòng là vị trí cắt trôi theo ⇒ MỌI đơn vị đổi text ⇒ trượt cache.
        // Đơn vị phải là CÂU: cuộn 1 dòng chỉ đổi 1-2 câu, phần còn lại trúng cache.
        TranslationCache cache;
        DocumentTranslator translator(&cache);

        // Một ĐOẠN dài 12 câu, ngắt dòng GIỮA câu (không dòng nào kết thúc bằng dấu chấm — trang
        // game/visual novel thường vậy, nên cả trang thành MỘT đoạn).
        const auto makePage = [](int first) {
            QStringList sentences;
            for (int i = first; i < first + 12; ++i)
                sentences.append(
                    QStringLiteral("Sentence number %1 talks about something interesting").arg(i));
            const QString text = sentences.join(QStringLiteral(". ")) + QLatin1Char('.');
            QStringList wrapped;
            for (int i = 0; i < text.size(); i += 40)
                wrapped.append(text.mid(i, 40));
            return wrapped.join(QLatin1Char('\n'));
        };

        int engineCalls = 0;
        const auto fn = [&engineCalls](const QStringList& units) {
            ++engineCalls;
            QStringList out;
            for (const QString& unit : units)
                out.append(QStringLiteral("VI:") + unit);
            return out;
        };

        const QString pageA = makePage(1);
        const TranslatedDocument first = translator.translate(pageA, "en", "vi", fn);
        QCOMPARE(first.newTranslations, 12);

        // Cuộn 1 dòng: dòng đầu rời đi, một câu mới vào cuối ⇒ chỉ 1-2 đơn vị mới.
        const QString pageB =
            pageA.mid(41) + QLatin1Char('\n') + QStringLiteral("Sentence number 13 is brand new.");
        const TranslatedDocument second = translator.translate(pageB, "en", "vi", fn);
        QVERIFY2(second.newTranslations <= 2,
                 qPrintable(QStringLiteral("cuộn 1 dòng chỉ được dịch mới ≤2 câu, thực tế %1")
                                .arg(second.newTranslations)));
        QVERIFY(second.complete);
    }

    void document_keepsCodeAndPageNumberOutsideTranslation()
    {
        DocumentTranslator translator;
        QStringList sent;
        const auto doc = translator.translate(
            QStringLiteral("An explanatory sentence.\n// Returns an instance of Responder\nPattern timeMatcher = Pattern.compile(x);\n56\nAnother paragraph."),
            QStringLiteral("en"), QStringLiteral("vi"), [&sent](const QStringList& units) {
                sent = units;
                return QStringList(units.size(), QStringLiteral("Da dich"));
            });
        QCOMPARE(doc.sourceParagraphs.size(), 5);
        QCOMPARE(doc.paragraphs.at(1), QStringLiteral("// Returns an instance of Responder"));
        QCOMPARE(doc.paragraphs.at(2), QStringLiteral("Pattern timeMatcher = Pattern.compile(x);"));
        QCOMPARE(doc.paragraphs.at(3), QStringLiteral("56"));
        QCOMPARE(sent.size(), 2);
        QVERIFY(doc.complete);
    }
};

QTEST_MAIN(TestTranslation)
#include "test_translation.moc"
