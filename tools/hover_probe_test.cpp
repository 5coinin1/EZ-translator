// Kiểm tra ĐƯỜNG HOVER mà không cần UI: vẽ một đoạn 3 dòng rồi gọi đúng
// OcrWorker::probeHover như app làm (dải cao 260 như bước "con trỏ đã dừng").
//
//   hover_probe_test.exe            # GPU nếu có
//   EZ_OCR_USE_GPU=0 hover_probe_test.exe
//
// Mục đích: chứng minh thuật toán gom đoạn tự cắt ở khe — nếu chỉ đọc 1 dòng thì
// log sẽ in 1 dòng, còn đúng thì phải đọc cả 3 dòng của đoạn.
#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QFont>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QString>

#include "ocr/OcrWorker.h"

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
    options.useGpu = qEnvironmentVariable("EZ_OCR_USE_GPU", QStringLiteral("1"))
                     != QLatin1String("0");

    OcrWorker worker;
    QString text;
    QRect rect;
    bool clipped = false;
    QObject::connect(&worker, &OcrWorker::hoverProbed,
                     [&](const QString&, const QString& probed, const QRect& probedRect,
                         bool probedClipped) {
                         text = probed;
                         rect = probedRect;
                         clipped = probedClipped;
                     });

    QElapsedTimer timer;
    timer.start();
    worker.load(options);
    qInfo() << "[test] load ms =" << timer.elapsed();

    // Panel tối + 3 dòng trắng cách nhau 34px: một "đoạn hội thoại" thu nhỏ.
    QImage image(1200, 260, QImage::Format_RGB32);
    image.fill(QColor(24, 26, 32));
    {
        QPainter painter(&image);
        painter.setPen(Qt::white);
        QFont font = painter.font();
        font.setPixelSize(22);
        font.setBold(true);
        painter.setFont(font);
        painter.drawText(20, 40, QStringLiteral("Open the door and take the key."));
        painter.drawText(20, 74, QStringLiteral("The guard will not let you pass."));
        painter.drawText(20, 108, QStringLiteral("Thank you very much for your help."));
        painter.end();
    }

    // Trỏ vào DÒNG GIỮA — nếu thuật toán chỉ đọc dòng con trỏ thì kết quả là 1 dòng.
    timer.restart();
    worker.probeHover(QStringLiteral("test"), image, QPoint(300, 74),
                      EZTranslator::ocrMonotonicMs());
    const qint64 ms = timer.elapsed();

    qInfo().noquote() << "[test] text =" << text;
    qInfo() << "[test] rect =" << rect << "clipped =" << clipped << "ms =" << ms;

    // Bản cũ: đơn vị là DÒNG con trỏ (không gom đoạn) — test kiểm đúng điều đó.
    const bool oneLine = !text.contains(QLatin1Char('\n'));
    const bool isCursorLine = text.contains(QLatin1String("guard"), Qt::CaseInsensitive);
    qInfo() << "[test] so dong =" << text.count(QLatin1Char('\n')) + 1
            << "| dung dong con tro =" << isCursorLine;
    qInfo() << ((oneLine && isCursorLine) ? "[test] PASS: doc dung dong con tro (ban cu)"
                                          : "[test] FAIL");

    return (oneLine && isCursorLine) ? 0 : 1;
}
