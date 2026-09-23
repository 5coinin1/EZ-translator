// Smoke test OCR: render ảnh tổng hợp có chữ rồi chạy Detection + Recognition,
// in box/text/timing. Dùng để xác nhận model + ONNX Runtime hoạt động.
//
//   ocr_smoke.exe            (dùng model trong EZ_DEFAULT_MODELS_DIR)
//   EZ_MODELS_DIR=<dir> ocr_smoke.exe
#include <QFont>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QString>
#include <QDebug>

#include "ocr/OcrEngine.h"
#include "ocr/OcrTextAssembler.h"

using namespace EZTranslator;

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);

    const QString dir =
        qEnvironmentVariable("EZ_MODELS_DIR", QStringLiteral(EZ_DEFAULT_MODELS_DIR));

    OcrOptions options;
    options.detModelPath = dir + QStringLiteral("/ch_PP-OCRv4_det_infer.onnx");
    options.recModelPath = dir + QStringLiteral("/en_PP-OCRv4_rec_mobile.onnx");
    options.dictionaryPath = dir + QStringLiteral("/en_dict.txt");
    options.useGpu = true;

    OcrEngine engine;
    QString error;
    if (!engine.load(options, &error)) {
        qWarning() << "load failed:" << error;
        return 1;
    }
    qInfo() << "provider:" << engine.providerName();

    QImage image(640, 240, QImage::Format_RGB32);
    image.fill(Qt::white);
    {
        QPainter painter(&image);
        painter.setPen(Qt::black);
        painter.setFont(QFont(QStringLiteral("Arial"), 24));
        painter.drawText(QRect(20, 20, 600, 50), Qt::AlignLeft | Qt::AlignVCenter,
                         QStringLiteral("Hello world"));
        painter.drawText(QRect(20, 90, 600, 50), Qt::AlignLeft | Qt::AlignVCenter,
                         QStringLiteral("Real time translation"));
        painter.drawText(QRect(20, 160, 600, 50), Qt::AlignLeft | Qt::AlignVCenter,
                         QStringLiteral("The quick brown fox"));
    }

    const QList<OcrTextBox> boxes = engine.recognize(image);
    const OcrTimings timings = engine.lastTimings();
    qInfo("det %.1f ms, rec %.1f ms, boxes %d", timings.detMs, timings.recMs, timings.boxes);
    for (const OcrTextBox& box : boxes)
        qInfo().noquote() << "  box" << box.rect << "conf" << box.confidence << "text:" << box.text;
    qInfo().noquote() << "assemble:\n" << OcrTextAssembler::assemble(boxes);
    return boxes.isEmpty() ? 2 : 0;
}
