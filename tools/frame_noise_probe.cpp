// Đo nhiễu khung hình THẬT của đường capture WGC — con số quyết định việc cache box so hash
// TUYỆT ĐỐI có trượt mỗi khung hay không (log thật của người dùng: OCR full ~17 lần/giây trên
// trang đứng yên, tức cache trượt mọi khung dù mắt thường thấy y hệt).
//
//   frame_noise_probe.exe <một phần tựa đề cửa sổ> [số giây]
//
// In ra: số khung nhận được, phân bố khoảng cách Hamming giữa dHash các khung liên tiếp và so
// với khung đầu, và số lần "sẽ OCR" mô phỏng theo logic CŨ (khớp tuyệt đối) vs MỚI (bỏ qua khi
// khoảng cách ≤ maxDistance như scheduler).
#include <QGuiApplication>
#include <QDebug>
#include <QEventLoop>
#include <QFile>
#include <QTextStream>
#include <QTimer>
#include <QVector>

#include <algorithm>
#include <utility>

#include "capture/GdiWindowCapture.h"
#include "capture/WgcWindowCapture.h"
#include "detection/ImageHash.h"

// windows.h phải SAU các header dự án: nó định nghĩa macro `small` (rpcndr.h) làm vỡ ImageHash.h.
#include <windows.h>

using namespace EZTranslator;

namespace {

// Console của môi trường này không hiện qInfo → ghi báo cáo ra file cạnh exe.
void reportHandler(QtMsgType, const QMessageLogContext&, const QString& msg)
{
    QFile file(QStringLiteral("frame_noise_probe_report.txt"));
    if (file.open(QIODevice::WriteOnly | QIODevice::Append)) {
        QTextStream stream(&file);
        stream << msg << "\n";
    }
}

struct FindState
{
    const QString* wanted;
    HWND found;
};

BOOL CALLBACK findWindowProc(HWND hwnd, LPARAM lparam)
{
    auto* state = reinterpret_cast<FindState*>(lparam);
    if (!IsWindowVisible(hwnd))
        return TRUE;
    wchar_t title[512] = {};
    GetWindowTextW(hwnd, title, 511);
    const QString text = QString::fromWCharArray(title);
    if (!text.isEmpty() && text.contains(*state->wanted, Qt::CaseInsensitive)) {
        state->found = hwnd;
        return FALSE;
    }
    return TRUE;
}

void describe(const QVector<int>& values, const char* label)
{
    if (values.isEmpty())
        return;
    QVector<int> sorted = values;
    std::sort(sorted.begin(), sorted.end());
    const int median = sorted.at(sorted.size() / 2);
    const int p90 = sorted.at(qMin(sorted.size() - 1, int(sorted.size() * 0.9)));
    qInfo().noquote() << QStringLiteral("%1: min %2 | median %3 | p90 %4 | max %5")
                             .arg(QString::fromLatin1(label))
                             .arg(sorted.first())
                             .arg(median)
                             .arg(p90)
                             .arg(sorted.last());
}

} // namespace

int main(int argc, char** argv)
{
    QFile::remove(QStringLiteral("frame_noise_probe_report.txt"));
    qInstallMessageHandler(reportHandler);
    QGuiApplication app(argc, argv);
    if (argc < 2) {
        qInfo() << "usage: frame_noise_probe.exe <tua de cua so> [giay]";
        return 2;
    }
    const QString wanted = QString::fromLocal8Bit(argv[1]);
    const int seconds = argc > 2 ? QByteArray(argv[2]).toInt() : 5;

    FindState state{&wanted, nullptr};
    EnumWindows(findWindowProc, reinterpret_cast<LPARAM>(&state));
    if (!state.found) {
        qInfo() << "khong tim thay cua so:" << wanted;
        return 3;
    }
    qInfo() << "cua so:" << wanted << "| hwnd =" << quintptr(state.found);

    WgcWindowCapture capture;
    QVector<QImage> frames;
    QObject::connect(&capture, &WgcWindowCapture::frameReady, [&frames](const CapturedFrame& frame) {
        if (frames.size() < 5000)
            frames.append(frame.image);
    });
    GdiWindowCapture gdi;
    QObject::connect(&gdi, &GdiWindowCapture::frameReady, [&frames](const CapturedFrame& frame) {
        if (frames.size() < 5000)
            frames.append(frame.image);
    });
    bool started = capture.start(quintptr(state.found));
    if (!started) {
        qInfo() << "WGC khong start duoc -> thu GDI";
        started = gdi.start(quintptr(state.found));
    }
    if (!started) {
        qInfo() << "khong start duoc capture (ca WGC lan GDI)";
        return 4;
    }

    QEventLoop loop;
    QTimer::singleShot(seconds * 1000, &loop, &QEventLoop::quit);
    loop.exec();
    capture.stop();
    gdi.stop();

    if (frames.size() < 3) {
        qInfo() << "chi nhan" << frames.size() << "khung";
        return 5;
    }

    QVector<quint64> hashes;
    hashes.reserve(frames.size());
    for (const QImage& image : frames)
        hashes.append(imageDifferenceHash(image));

    QVector<int> consecutive;
    QVector<int> vsFirst;
    consecutive.reserve(hashes.size() - 1);
    vsFirst.reserve(hashes.size() - 1);
    for (int i = 1; i < hashes.size(); ++i) {
        consecutive.append(imageHammingDistance(hashes[i - 1], hashes[i]));
        vsFirst.append(imageHammingDistance(hashes[0], hashes[i]));
    }

    qInfo().noquote() << QStringLiteral("khung: %1 | kich thuoc %2x%3")
                             .arg(frames.size())
                             .arg(frames.first().width())
                             .arg(frames.first().height());
    describe(consecutive, "Hamming lien tiep  ");
    describe(vsFirst, "Hamming so khung dau");

    // Mô phỏng đúng vòng tick: logic CŨ (cache khớp TUYỆT ĐỐI) vs MỚI (bỏ qua khi ≤ maxDistance).
    const int maxDistance = 4;
    int oldRuns = 0;
    int newRuns = 0;
    quint64 oldServed = 0;
    bool oldHas = false;
    quint64 newServed = 0;
    bool newHas = false;
    for (quint64 hash : hashes) {
        if (!oldHas || hash != oldServed) {
            ++oldRuns;
            oldServed = hash;
            oldHas = true;
        }
        if (!newHas || imageHammingDistance(hash, newServed) > maxDistance) {
            ++newRuns;
            newServed = hash;
            newHas = true;
        }
    }
    qInfo().noquote() << QStringLiteral(
                             "Mo phong %1 khung: OCR/dich lai theo logic CU = %2 lan | logic MOI = %3 lan")
                             .arg(hashes.size())
                             .arg(oldRuns)
                             .arg(newRuns);
    return 0;
}
