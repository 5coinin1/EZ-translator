// Smoke/bench engine dịch — đi đúng chuỗi ưu tiên của app:
//   ct2 int8 (nhanh nhất) → Marian ONNX → online (Google + MyMemory)
//
//   translate_smoke.exe
//   EZ_SRC=zh EZ_TGT=vi translate_smoke.exe
//   EZ_MT_BENCH=4 translate_smoke.exe     # đo ms/câu của engine đang dùng
//   EZ_TRANSLATE_ENGINE=marian ...        # ép dùng Marian để A/B
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QList>
#include <QString>
#include <QStringList>

#include "translation/CTranslate2Translator.h"
#include "translation/DocumentTranslator.h"
#include "translation/GoogleTranslator.h"
#include "translation/MarianTranslator.h"
#include "translation/MyMemoryTranslator.h"
#include "translation/TranslationCache.h"

using namespace EZTranslator;

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    const QString src = qEnvironmentVariable("EZ_SRC", QStringLiteral("en"));
    const QString tgt = qEnvironmentVariable("EZ_TGT", QStringLiteral("vi"));
    const QString dir =
        qEnvironmentVariable("EZ_MODELS_DIR", QStringLiteral(EZ_DEFAULT_MODELS_DIR));
    const QString engineMode = qEnvironmentVariable("EZ_TRANSLATE_ENGINE",
                                                    QStringLiteral("auto"));

    // ── Chọn engine, đúng thứ tự ưu tiên của TranslationWorker ──────────────
    CTranslate2Translator ct2;
    MarianTranslator marian;
    GoogleTranslator google;
    MyMemoryTranslator mymemory;
    bool ct2Ready = false;
    bool marianReady = false;

    if (src == QLatin1String("en") && tgt == QLatin1String("vi")
        && engineMode != QLatin1String("online")) {
        if (engineMode != QLatin1String("marian")) {
            const QString ct2Dir = dir + QStringLiteral("/ct2-en-vi-int8");
            if (QDir(ct2Dir).exists()) {
                const QString python = CTranslate2Translator::findPython();
                if (python.isEmpty()) {
                    qInfo() << "ct2 : bỏ qua — không có Python + ctranslate2 (đặt EZ_PYTHON)";
                } else {
                    QElapsedTimer loadTimer;
                    loadTimer.start();
                    QString error;
                    ct2Ready = ct2.start(python, QString(), ct2Dir, QStringLiteral("int8"), src,
                                         tgt, &error);
                    qInfo("ct2 : load=%d (%.0f ms) python=%s %s", ct2Ready ? 1 : 0,
                          double(loadTimer.elapsed()), qUtf8Printable(python),
                          qUtf8Printable(error));
                }
            }
        }

        if (!ct2Ready) {
            const QString modelDir = dir + QStringLiteral("/opus-mt-en-vi");
            if (QFile::exists(modelDir + QStringLiteral("/encoder_model.onnx"))) {
                MarianOptions options;
                options.encoderModelPath = modelDir + QStringLiteral("/encoder_model.onnx");
                options.decoderModelPath = modelDir + QStringLiteral("/decoder_model.onnx");
                options.decoderWithPastModelPath =
                    modelDir + QStringLiteral("/decoder_with_past_model.onnx");
                options.sourceSpmPath = modelDir + QStringLiteral("/source.spm");
                options.targetSpmPath = modelDir + QStringLiteral("/target.spm");
                options.vocabPath = modelDir + QStringLiteral("/vocab.json");
                options.useGpu = !qEnvironmentVariableIsSet("EZ_MT_CPU");
                options.debug = qEnvironmentVariableIsSet("EZ_MT_DEBUG");

                QElapsedTimer loadTimer;
                loadTimer.start();
                QString error;
                marianReady = marian.load(options, &error);
                qInfo("marian: load=%d (%.0f ms) %s", marianReady ? 1 : 0,
                      double(loadTimer.elapsed()), qUtf8Printable(error));
            }
        }
    }

    const QString engineName = ct2Ready ? QStringLiteral("ct2 int8")
                               : marianReady ? QStringLiteral("Marian ONNX")
                                             : QStringLiteral("online");

    const auto translateUnits = [&](const QStringList& units) -> QStringList {
        if (ct2Ready)
            return ct2.translate(units, src, tgt);
        if (marianReady)
            return marian.translate(units, src, tgt);

        QStringList out = google.translate(units, src, tgt);
        QStringList missing;
        QList<int> missingIndex;
        for (int i = 0; i < out.size() && i < units.size(); ++i) {
            if (out.at(i).isEmpty() && !units.at(i).trimmed().isEmpty()) {
                missing.append(units.at(i));
                missingIndex.append(i);
            }
        }
        if (!missing.isEmpty()) {
            const QStringList alt = mymemory.translate(missing, src, tgt);
            for (int k = 0; k < alt.size() && k < missingIndex.size(); ++k) {
                if (!alt.at(k).isEmpty())
                    out[missingIndex.at(k)] = alt.at(k);
            }
        }
        return out;
    };

    // ── Đo độ trễ từng câu (thứ quyết định tốc độ hover), có cả IPC với sidecar ──
    const int rounds = qEnvironmentVariableIntValue("EZ_MT_BENCH");
    if (rounds > 0 && (ct2Ready || marianReady)) {
        const QStringList lines = {
            QStringLiteral("Open the door"),
            QStringLiteral("Where is the key?"),
            QStringLiteral("Thank you very much"),
            QStringLiteral("The guard will not let you pass"),
            QStringLiteral("I have been waiting for you for a long time"),
            QStringLiteral("Take this potion and you will feel better"),
        };
        for (const QString& line : lines)
            translateUnits({line}); // bỏ lượt đầu
        QList<qint64> samples;
        for (int round = 0; round < rounds; ++round) {
            for (const QString& line : lines) {
                QElapsedTimer timer;
                timer.start();
                translateUnits({line});
                samples.append(timer.elapsed());
            }
        }
        std::sort(samples.begin(), samples.end());
        qint64 sum = 0;
        for (qint64 value : samples)
            sum += value;
        qInfo("BENCH %s: n=%d min=%lld ms median=%lld ms mean=%.1f ms", qUtf8Printable(engineName),
              int(samples.size()), samples.first(), samples.at(samples.size() / 2),
              double(sum) / double(samples.size()));
    }

    // ── Bản dịch mẫu ────────────────────────────────────────────────────────
    const QString text = QStringLiteral(
        "Good morning, how are you?\n\nOpen the door\n\n"
        "I am very happy today\n\nWhere is the key?\n\nThank you very much\n\nHello world");

    TranslationCache cache;
    DocumentTranslator document(&cache);
    QElapsedTimer timer;
    timer.start();
    const TranslatedDocument doc = document.translate(text, src, tgt, translateUnits);
    const qint64 elapsed = timer.elapsed();

    qInfo() << "engine =" << engineName << "| from" << src << "to" << tgt
            << "| complete =" << doc.complete << "| new =" << doc.newTranslations << "|" << elapsed
            << "ms";

    QFile out(QStringLiteral("translate_smoke.out.txt"));
    if (out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QString report = QStringLiteral("engine = %1 | %2 -> %3 | %4 ms\n")
                             .arg(engineName, src, tgt)
                             .arg(elapsed);
        for (int i = 0; i < doc.sourceParagraphs.size(); ++i) {
            report += QStringLiteral("  [%1] -> [%2]\n")
                          .arg(doc.sourceParagraphs.at(i), doc.paragraphs.value(i));
        }
        out.write(report.toUtf8());
    }

    return doc.complete ? 0 : 1;
}
