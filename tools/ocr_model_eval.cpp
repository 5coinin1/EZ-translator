#include "ocr/OcrEngine.h"
#include "ocr/OcrThreadBudget.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QTextStream>

#include <algorithm>

using namespace EZTranslator;

namespace {

QString compactText(QString text)
{
    return text.simplified().toCaseFolded();
}

int editDistance(const QString& lhs, const QString& rhs)
{
    QVector<int> previous(rhs.size() + 1);
    QVector<int> current(rhs.size() + 1);
    for (int j = 0; j <= rhs.size(); ++j)
        previous[j] = j;
    for (int i = 1; i <= lhs.size(); ++i) {
        current[0] = i;
        for (int j = 1; j <= rhs.size(); ++j) {
            const int substitution = previous[j - 1] + (lhs.at(i - 1) == rhs.at(j - 1) ? 0 : 1);
            current[j] = std::min({current[j - 1] + 1, previous[j] + 1, substitution});
        }
        std::swap(previous, current);
    }
    return previous[rhs.size()];
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    const QString dataset = qEnvironmentVariable("EZ_OCR_EVAL_DATASET");
    const QString models = qEnvironmentVariable("EZ_MODELS_DIR", QStringLiteral(EZ_DEFAULT_MODELS_DIR));
    const QString rec = qEnvironmentVariable(
        "EZ_OCR_EVAL_REC_MODEL", models + QStringLiteral("/en_PP-OCRv4_rec_mobile.onnx"));
    const QString dict = qEnvironmentVariable("EZ_OCR_EVAL_DICT", models + QStringLiteral("/en_dict.txt"));
    if (dataset.isEmpty() || !QDir(dataset).exists()) {
        QTextStream(stderr) << "EZ_OCR_EVAL_DATASET must point to a directory of PNG/JPG images\n";
        return 2;
    }

    OcrOptions options;
    options.detModelPath = models + QStringLiteral("/ch_PP-OCRv4_det_infer.onnx");
    options.recModelPath = rec;
    options.dictionaryPath = dict;
    options.lineCacheLimit = 0;
    options.useGpu = qEnvironmentVariable("EZ_OCR_USE_GPU", QStringLiteral("1")) != QStringLiteral("0");
    const OcrThreadBudget budget = chooseOcrThreadBudget(options.useGpu, false);
    options.threads = budget.detectorIntraOp;
    options.recThreads = budget.recognizerIntraOp;
    options.recWorkers = budget.recognizerWorkers;
    options.interOpThreads = budget.interOp;

    OcrEngine engine;
    QString error;
    if (!engine.load(options, &error)) {
        QTextStream(stderr) << "OCR model load failed: " << error << '\n';
        return 3;
    }

    QTextStream out(stdout);
    out << "image,expected,actual,cer,exact,boxes,totalMs\n";
    int samples = 0;
    double totalCer = 0.0;
    int exact = 0;
    QDirIterator it(dataset, {QStringLiteral("*.png"), QStringLiteral("*.jpg"),
                              QStringLiteral("*.jpeg")}, QDir::Files);
    while (it.hasNext()) {
        const QFileInfo imageInfo(it.next());
        QFile expectedFile(imageInfo.path() + QLatin1Char('/')
                           + imageInfo.completeBaseName() + QStringLiteral(".txt"));
        if (!expectedFile.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        const QString expected = compactText(QString::fromUtf8(expectedFile.readAll()));
        const QImage image(imageInfo.filePath());
        const QList<OcrTextBox> boxes = engine.recognize(image);
        QStringList texts;
        for (const OcrTextBox& box : boxes)
            if (!box.text.trimmed().isEmpty())
                texts.append(box.text.trimmed());
        const QString actual = compactText(texts.join(QLatin1Char(' ')));
        const qsizetype denominator = std::max<qsizetype>(1, expected.size());
        const double cer = double(editDistance(expected, actual)) / double(denominator);
        totalCer += cer;
        const bool isExact = actual == expected;
        exact += isExact ? 1 : 0;
        ++samples;
        out << imageInfo.fileName() << ',' << expected.size() << ',' << actual.size() << ','
            << cer << ',' << (isExact ? 1 : 0) << ',' << boxes.size() << ','
            << engine.lastTimings().totalMs << '\n';
    }

    if (samples == 0) {
        QTextStream(stderr) << "No image + .txt pairs found in dataset\n";
        return 4;
    }
    QTextStream(stderr) << "provider=" << engine.providerName() << " samples=" << samples
                        << " meanCER=" << (totalCer / samples)
                        << " exact=" << exact << '/' << samples << '\n';
    Q_UNUSED(args);
    return 0;
}
