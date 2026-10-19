#include "ocr/OcrEngine.h"

#include "ocr/OcrDetector.h"
#include "ocr/OcrRecognizer.h"
#include "ocr/OcrRuntime.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QImage>

namespace EZTranslator {

namespace {

/**
 * Nhị phân hoá Otsu trên kênh xám (thử nghiệm, Cài đặt → OCR).
 *
 * Với UI tương phản thấp (chữ xám trên nền xám, chữ có viền mờ), ngưỡng Otsu làm
 * biên chữ rõ hơn cho cả detector lẫn recognizer. Trả ảnh RGB32 0/255 để hai model
 * nhận đúng phân bố màu như ảnh thường.
 */
QImage binarizeImage(const QImage& source)
{
    const QImage gray = source.convertToFormat(QImage::Format_Grayscale8);
    if (gray.isNull() || gray.width() <= 0 || gray.height() <= 0)
        return source;

    int histogram[256] = {};
    for (int y = 0; y < gray.height(); ++y) {
        const uchar* row = gray.constScanLine(y);
        for (int x = 0; x < gray.width(); ++x)
            ++histogram[row[x]];
    }

    const int total = gray.width() * gray.height();
    double sum = 0.0;
    for (int i = 0; i < 256; ++i)
        sum += double(i) * histogram[i];

    double sumBackground = 0.0;
    int weightBackground = 0;
    double bestVariance = -1.0;
    int threshold = 128;
    for (int t = 0; t < 256; ++t) {
        weightBackground += histogram[t];
        if (weightBackground == 0)
            continue;
        const int weightForeground = total - weightBackground;
        if (weightForeground == 0)
            break;
        sumBackground += double(t) * histogram[t];
        const double meanBackground = sumBackground / weightBackground;
        const double meanForeground = (sum - sumBackground) / weightForeground;
        const double variance = double(weightBackground) * double(weightForeground)
                                * (meanBackground - meanForeground) * (meanBackground - meanForeground);
        if (variance > bestVariance) {
            bestVariance = variance;
            threshold = t;
        }
    }

    QImage out(gray.size(), QImage::Format_RGB32);
    for (int y = 0; y < gray.height(); ++y) {
        const uchar* srcRow = gray.constScanLine(y);
        auto* dstRow = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < gray.width(); ++x) {
            const int value = srcRow[x] > threshold ? 255 : 0;
            dstRow[x] = qRgb(value, value, value);
        }
    }
    return out;
}

} // namespace

// Dưới mức này coi như model SAI TỪ ĐIỂN (đọc ra rác): thử model hệ chữ còn lại rồi chọn bên tốt
// hơn. Model đúng trên chữ sạch thường ~0.9+; model sai hệ chữ thấp hơn hẳn.
constexpr double kLowConfidence = 0.80;

/** Điểm tin cậy trung bình của các dòng đọc được (0 = không đọc được gì). */
double meanConfidence(const QList<OcrTextBox>& boxes)
{
    double sum = 0.0;
    int count = 0;
    for (const OcrTextBox& box : boxes) {
        if (box.text.trimmed().isEmpty())
            continue;
        sum += box.confidence;
        ++count;
    }
    return count > 0 ? sum / count : 0.0;
}

struct OcrEngine::Impl
{
    OcrRuntime runtime;
    OcrDetector detector;
    OcrRecognizer recognizer;
    /// Model chữ của HỆ CHỮ KHÁC (en ↔ zh), nạp theo nhu cầu khi model chính đọc ra rác.
    OcrRecognizer altRecognizer;
    bool altLoaded{false};
    bool altTried{false};
    bool preferAlt{false}; ///< model dự phòng đang thắng — chạy nó trước cho khỏi tốn 2 lượt rec
    OcrOptions options;
    bool loaded{false};
    bool binarize{false}; ///< Cài đặt → OCR: nhị phân hoá ảnh trước khi quét
    int lastBoxes{0};
};

OcrEngine::OcrEngine()
    : m_impl(std::make_unique<Impl>())
{
}

OcrEngine::~OcrEngine() = default;

bool OcrEngine::load(const OcrOptions& options, QString* error)
{
    if (!m_impl)
        return false;

    m_impl->runtime.configure(options.useGpu, error);
    m_impl->binarize = options.binarize;
    m_impl->options = options;

    if (!m_impl->detector.load(m_impl->runtime, options, error))
        return false;
    if (!m_impl->recognizer.load(m_impl->runtime, options, error))
        return false;

    m_impl->loaded = true;
    return true;
}

bool OcrEngine::isLoaded() const
{
    return m_impl && m_impl->loaded;
}

QString OcrEngine::providerName() const
{
    return m_impl ? m_impl->runtime.providerName() : QString();
}

QList<OcrTextBox> OcrEngine::recognize(const QImage& image)
{
    if (!isLoaded() || image.isNull())
        return {};

    const QImage input = m_impl->binarize ? binarizeImage(image) : image;
    const QList<OcrTextBox> boxes = m_impl->detector.detect(input);
    m_impl->lastBoxes = boxes.size();
    if (boxes.isEmpty())
        return {};

    // MODEL CHỮ THEO NỘI DUNG: ngôn ngữ nguồn trong Cài đặt có thể LỆCH với nội dung thật (đặt
    // "en" cho trang truyện rồi mở cửa sổ tiếng Trung). Model sai từ điển vẫn trả chữ nhưng là
    // RÁC với độ tin cậy thấp ⇒ bản dịch bị lọc bỏ ⇒ "không phản ứng gì" (cả overlay lẫn hover).
    // Điểm tin cậy thấp thì thử model hệ chữ còn lại, chọn bên đọc tốt hơn — và NHỚ bên thắng để
    // các khung sau chỉ chạy MỘT model (nội dung không đổi ngôn ngữ giữa chừng).
    if (m_impl->altLoaded && m_impl->preferAlt) {
        const QList<OcrTextBox> alt = m_impl->altRecognizer.recognize(input, boxes);
        if (meanConfidence(alt) >= kLowConfidence)
            return alt; // model dự phòng vẫn đọc tốt → dùng luôn
        m_impl->preferAlt = false; // đọc kém (đã đổi nội dung) → so sánh lại đầy đủ
    }

    const QList<OcrTextBox> primary = m_impl->recognizer.recognize(input, boxes);
    const double primaryConfidence = meanConfidence(primary);
    if (primaryConfidence < kLowConfidence && ensureAltRecognizer()) {
        const QList<OcrTextBox> alt = m_impl->altRecognizer.recognize(input, boxes);
        const double altConfidence = meanConfidence(alt);
        qInfo() << "[OCR] điểm tin cậy thấp" << primaryConfidence << "| model dự phòng"
                << altConfidence;
        if (altConfidence > primaryConfidence) {
            m_impl->preferAlt = true;
            return alt;
        }
    }
    return primary;
}

bool OcrEngine::ensureAltRecognizer()
{
    if (m_impl->altLoaded)
        return true;
    if (m_impl->altTried)
        return false; // đã thử và thất bại — không thử lại mỗi khung
    m_impl->altTried = true;

    const QString dir = QFileInfo(m_impl->options.recModelPath).absolutePath();
    const QString current = QFileInfo(m_impl->options.recModelPath).fileName();
    OcrOptions alt = m_impl->options;
    if (current.startsWith(QLatin1String("ch_"))) {
        alt.recModelPath = dir + QStringLiteral("/en_PP-OCRv4_rec_mobile.onnx");
        alt.dictionaryPath = dir + QStringLiteral("/en_dict.txt");
    } else if (current.startsWith(QLatin1String("japan")) || current.startsWith(QLatin1String("ja_"))) {
        // Model Nhật dự phòng về model Trung (cùng hệ chữ CJK) khi nội dung hoá ra là tiếng Trung.
        alt.recModelPath = dir + QStringLiteral("/ch_PP-OCRv4_rec_infer.onnx");
        alt.dictionaryPath = dir + QStringLiteral("/ppocr_keys_v1.txt");
    } else {
        alt.recModelPath = dir + QStringLiteral("/ch_PP-OCRv4_rec_infer.onnx");
        alt.dictionaryPath = dir + QStringLiteral("/ppocr_keys_v1.txt");
    }
    if (!QFile::exists(alt.recModelPath) || !QFile::exists(alt.dictionaryPath))
        return false;
    QString error;
    if (!m_impl->altRecognizer.load(m_impl->runtime, alt, &error)) {
        qWarning() << "[OCR] model chữ dự phòng nạp thất bại:" << error;
        return false;
    }
    m_impl->altLoaded = true;
    qInfo() << "[OCR] đã nạp model chữ dự phòng:" << QFileInfo(alt.recModelPath).fileName();
    return true;
}

OcrTimings OcrEngine::lastTimings() const
{
    OcrTimings timings = m_impl ? m_impl->recognizer.lastTimings() : OcrTimings();
    if (m_impl) {
        timings.detMs = m_impl->detector.lastMs();
        timings.boxes = m_impl->lastBoxes;
    }
    return timings;
}

OcrDetector& OcrEngine::detector()
{
    return m_impl->detector;
}

OcrRecognizer& OcrEngine::recognizer()
{
    return m_impl->recognizer;
}

} // namespace EZTranslator
