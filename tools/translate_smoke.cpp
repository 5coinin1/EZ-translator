// Smoke test dịch: dịch vài câu en->vi qua engine online (Google + dự phòng MyMemory).
//
//   translate_smoke.exe
//   EZ_SRC=zh EZ_TGT=vi translate_smoke.exe
#include <QCoreApplication>
#include <QDebug>
#include <QList>
#include <QString>
#include <QStringList>

#include "translation/DocumentTranslator.h"
#include "translation/GoogleTranslator.h"
#include "translation/MyMemoryTranslator.h"
#include "translation/TranslationCache.h"

using namespace EZTranslator;

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    const QString src = qEnvironmentVariable("EZ_SRC", QStringLiteral("en"));
    const QString tgt = qEnvironmentVariable("EZ_TGT", QStringLiteral("vi"));

    TranslationCache cache;
    GoogleTranslator google;
    MyMemoryTranslator mymemory;
    DocumentTranslator document(&cache);

    const auto translateFn = [&](const QStringList& units) {
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

    const QString text = QStringLiteral("Hello world\n\nGood morning, how are you?");
    const TranslatedDocument doc = document.translate(text, src, tgt, translateFn);

    qInfo() << "from" << src << "to" << tgt << "complete =" << doc.complete
            << "new =" << doc.newTranslations;
    for (int i = 0; i < doc.sourceParagraphs.size(); ++i)
        qInfo().noquote() << "  [" << doc.sourceParagraphs.at(i) << "] -> ["
                          << doc.paragraphs.value(i) << "]";

    return doc.complete ? 0 : 1;
}
