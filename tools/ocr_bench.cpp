// Bench OCR cho chế độ hover: đo det/rec, và kiểm tra DML có biên dịch lại khi
// kích thước input thay đổi không.
//
//   ocr_bench.exe            (dùng model trong EZ_DEFAULT_MODELS_DIR)
//   EZ_OCR_USE_GPU=0 ocr_bench.exe
#include <QElapsedTimer>
#include <QFont>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QDebug>
#include <QTextStream>

#include "ocr/OcrDetector.h"
#include "ocr/OcrEngine.h"
#include "ocr/OcrRecognizer.h"
#include "ocr/OcrThreadBudget.h"

using namespace EZTranslator;

namespace {

QImage makeScreen(const QSize& size = QSize(1280, 720))
{
    QImage screen(size, QImage::Format_RGB32);
    screen.fill(QColor(210, 214, 220));
    QPainter painter(&screen);
    painter.setPen(Qt::black);
    painter.setFont(QFont(QStringLiteral("Arial"), 20));
    const QStringList lines = {
        QStringLiteral("Chapter one the beginning of the story"),
        QStringLiteral("The quick brown fox jumps over the lazy dog"),
        QStringLiteral("Real time translation for games and novels"),
        QStringLiteral("Performance of detection and recognition stage"),
        QStringLiteral("Hover translate probe line number five"),
        QStringLiteral("End of the sample text for the benchmark"),
    };
    for (int i = 0; i < lines.size(); ++i)
        painter.drawText(QRect(40, 60 + i * (size.height() / 7), size.width() - 80, 60),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         lines.at(i));
    return screen;
}

double msSince(const QElapsedTimer& timer)
{
    return double(timer.nsecsElapsed()) / 1e6;
}

} // namespace

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    const bool csv = app.arguments().contains(QStringLiteral("--csv"));
    QTextStream csvOut(stdout);
    if (csv) {
        csvOut << "path,iteration,provider,totalMs,detMs,recMs,detCalls,tileCount,altCalls,"
                   "boxes,recCalls,recCached\n";
    }

    const QString dir =
        qEnvironmentVariable("EZ_MODELS_DIR", QStringLiteral(EZ_DEFAULT_MODELS_DIR));

    OcrOptions options;
    options.detModelPath = dir + QStringLiteral("/ch_PP-OCRv4_det_infer.onnx");
    options.recModelPath = dir + QStringLiteral("/en_PP-OCRv4_rec_mobile.onnx");
    options.dictionaryPath = dir + QStringLiteral("/en_dict.txt");
    options.lineCacheLimit = 0; // đo rec THẬT (không lấy từ cache nội dung dòng)
    const QByteArray gpuEnv = qgetenv("EZ_OCR_USE_GPU");
    options.useGpu = gpuEnv.isEmpty() ? true : (gpuEnv != "0");
    const OcrThreadBudget budget = chooseOcrThreadBudget(options.useGpu, false);
    options.threads = budget.detectorIntraOp;
    options.recThreads = budget.recognizerIntraOp;
    options.recWorkers = budget.recognizerWorkers;
    options.interOpThreads = budget.interOp;
    qInfo() << "thread budget: det=" << options.threads << "rec=" << options.recThreads
            << "workers=" << options.recWorkers << "inter=" << options.interOpThreads;

    OcrEngine engine;
    QString error;
    if (!engine.load(options, &error)) {
        qWarning() << "load failed:" << error;
        return 1;
    }
    qInfo() << "provider:" << engine.providerName();

    const QImage screen = makeScreen();
    QElapsedTimer timer;

    // 1) det trên CÙNG một dải (đo ấm dần).
    const QRect fixedBand(40, 260, 1200, 90);
    const QImage fixedImage = screen.copy(fixedBand);
    for (int i = 0; i < 5; ++i) {
        timer.restart();
        const QList<OcrTextBox> rows = engine.detector().detect(fixedImage);
        if (csv) {
            csvOut << "det-same," << (i + 1) << "," << engine.providerName() << ","
                   << msSince(timer) << "," << engine.detector().lastMs()
                   << ",0,1,1,0," << rows.size() << ",0,0\n";
        }
        qInfo("det same #%d: %.1f ms rows=%d", i + 1, msSince(timer), int(rows.size()));
    }

    // 2) det CÙNG kích thước, KHÁC vị trí (như hover mới: shape luôn cố định).
    const QList<QRect> bands = {QRect(40, 60, 1200, 96),  QRect(40, 160, 1200, 96),
                                QRect(40, 260, 1200, 96), QRect(40, 360, 1200, 96),
                                QRect(40, 460, 1200, 96), QRect(40, 560, 1200, 96)};
    for (int i = 0; i < bands.size(); ++i) {
        const QImage image = screen.copy(bands.at(i));
        timer.restart();
        const QList<OcrTextBox> rows = engine.detector().detect(image);
        if (csv) {
            csvOut << "det-same-size," << (i + 1) << "," << engine.providerName() << ","
                   << msSince(timer) << "," << engine.detector().lastMs()
                   << ",0,1,1,0," << rows.size() << ",0,0\n";
        }
        qInfo("det same-size #%d: %.1f ms y=%d rows=%d", i + 1, msSince(timer), bands.at(i).y(),
              int(rows.size()));
    }

    // 3) rec THẬT: mỗi lần một dòng khác (không cache).
    for (int i = 0; i < 6; ++i) {
        const QRect band(40, 60 + i * 100 - 20, 1200, 90);
        const QImage image = screen.copy(band);
        const QList<OcrTextBox> rows = engine.detector().detect(image);
        if (rows.isEmpty()) {
            qInfo("rec #%d: (no row)", i + 1);
            continue;
        }
        const OcrTextBox row = rows.first();
        timer.restart();
        const QList<OcrTextBox> rec = engine.recognizer().recognize(image, QList<OcrTextBox>{row});
        const OcrTimings recTiming = engine.recognizer().lastTimings();
        if (csv) {
            csvOut << "rec," << (i + 1) << "," << engine.providerName() << ","
                   << msSince(timer) << ",0," << recTiming.recMs << ",0,1,0,"
                   << rec.size() << "," << recTiming.recCalls << "," << recTiming.recCached
                   << "\n";
        }
        qInfo("rec #%d: %.1f ms  text=%s", i + 1, msSince(timer),
              qUtf8Printable(rec.isEmpty() ? QString() : rec.first().text));
    }

    // End-to-end detector → recognizer baseline for the same hover-sized input.
    for (int i = 0; i < 3; ++i) {
        const QElapsedTimer pipelineTimer = [] {
            QElapsedTimer value;
            value.start();
            return value;
        }();
        const QList<OcrTextBox> result = engine.recognize(fixedImage);
        const OcrTimings timings = engine.lastTimings();
        if (csv) {
            csvOut << "pipeline-hover," << (i + 1) << "," << engine.providerName() << ","
                   << msSince(pipelineTimer) << "," << timings.detMs << "," << timings.recMs
                   << "," << timings.detCalls << "," << timings.tileCount << ","
                   << timings.altCalls << "," << result.size() << "," << timings.recCalls
                   << "," << timings.recCached << "\n";
        }
        qInfo("pipeline hover #%d: total=%.1f det=%.1f rec=%.1f boxes=%d calls=%d cached=%d",
              i + 1, timings.totalMs, timings.detMs, timings.recMs, int(result.size()),
              timings.recCalls, timings.recCached);
    }

    // End-to-end ROI nhiều dòng: workload này mới kích hoạt recWorkers CPU.
    for (int i = 0; i < 3; ++i) {
        const QElapsedTimer pipelineTimer = [] {
            QElapsedTimer value;
            value.start();
            return value;
        }();
        const QList<OcrTextBox> result = engine.recognize(screen);
        const OcrTimings timings = engine.lastTimings();
        if (csv) {
            csvOut << "pipeline-roi," << (i + 1) << "," << engine.providerName() << ","
                   << msSince(pipelineTimer) << "," << timings.detMs << "," << timings.recMs
                   << "," << timings.detCalls << "," << timings.tileCount << ","
                   << timings.altCalls << "," << result.size() << "," << timings.recCalls
                   << "," << timings.recCached << "\n";
        }
        qInfo("pipeline ROI #%d: total=%.1f det=%.1f rec=%.1f boxes=%d calls=%d cached=%d",
              i + 1, timings.totalMs, timings.detMs, timings.recMs, int(result.size()),
              timings.recCalls, timings.recCached);
    }

    // Full-window workload kích hoạt tiled path trên ảnh >=1600x900.
    const QImage largeScreen = makeScreen(QSize(1920, 1080));
    for (int i = 0; i < 3; ++i) {
        const QElapsedTimer pipelineTimer = [] {
            QElapsedTimer value;
            value.start();
            return value;
        }();
        const QList<OcrTextBox> result = engine.recognize(largeScreen);
        const OcrTimings timings = engine.lastTimings();
        if (csv) {
            csvOut << "pipeline-large," << (i + 1) << "," << engine.providerName() << ","
                   << msSince(pipelineTimer) << "," << timings.detMs << "," << timings.recMs
                   << "," << timings.detCalls << "," << timings.tileCount << ","
                   << timings.altCalls << "," << result.size() << "," << timings.recCalls
                   << "," << timings.recCached << "\n";
        }
        qInfo("pipeline large #%d: total=%.1f det=%.1f rec=%.1f boxes=%d calls=%d cached=%d",
              i + 1, timings.totalMs, timings.detMs, timings.recMs, int(result.size()),
              timings.recCalls, timings.recCached);
    }

    return 0;
}
