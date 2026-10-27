#include "ocr/OcrEngine.h"

#include "ocr/OcrDetector.h"
#include "ocr/OcrRecognizer.h"
#include "ocr/OcrRuntime.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QImage>

#include <algorithm>
#include <chrono>
#include <cmath>

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
    OcrRuntime detRuntime;
    OcrRuntime recRuntime;
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
    OcrTimings lastTiming;
};

void addRecognizerTiming(OcrTimings& total, const OcrTimings& part, bool alternate)
{
    total.recMs += part.recMs;
    total.recCalls += part.recCalls;
    total.recCached += part.recCached;
    total.recRunMs += part.recRunMs;
    total.recDecodeMs += part.recDecodeMs;
    total.recOutputBytes += part.recOutputBytes;
    total.recBatched = total.recBatched || part.recBatched;
    if (alternate)
        ++total.altCalls;
}

OcrEngine::OcrEngine()
    : m_impl(std::make_unique<Impl>())
{
}

OcrEngine::~OcrEngine() = default;

bool OcrEngine::load(const OcrOptions& options, QString* error)
{
    if (!m_impl)
        return false;

    m_impl->loaded = false;
    QString detError;
    QString recError;
    const QString fallbackDevice = options.useGpu ? QStringLiteral("auto") : QStringLiteral("cpu");
    const QString detDevice = options.detDevice.isEmpty() ? fallbackDevice : options.detDevice;
    const QString recDevice = options.recDevice.isEmpty() ? fallbackDevice : options.recDevice;
    Q_UNUSED(m_impl->detRuntime.configure(detDevice, options.gpuAdapterIndex, &detError));
    Q_UNUSED(m_impl->recRuntime.configure(recDevice, options.gpuAdapterIndex, &recError));
    m_impl->binarize = options.binarize;
    m_impl->options = options;
    m_impl->options.detDevice = detDevice;
    m_impl->options.recDevice = recDevice;

    if (!m_impl->detector.load(m_impl->detRuntime, options, &detError)) {
        if (error)
            *error = detError;
        return false;
    }
    if (!m_impl->recognizer.load(m_impl->recRuntime, options, &recError)) {
        if (error)
            *error = recError;
        return false;
    }
    if (error)
        *error = detError.isEmpty() ? recError : detError;

    m_impl->loaded = true;
    return true;
}

bool OcrEngine::isLoaded() const
{
    return m_impl && m_impl->loaded;
}

QString OcrEngine::providerName() const
{
    if (!m_impl)
        return {};
    return QStringLiteral("Det:%1/Rec:%2")
        .arg(m_impl->detRuntime.providerName(), m_impl->recRuntime.providerName());
}

QString OcrEngine::detProviderName() const
{
    return m_impl ? m_impl->detRuntime.providerName() : QString();
}

QString OcrEngine::recProviderName() const
{
    return m_impl ? m_impl->recRuntime.providerName() : QString();
}

QList<OcrTextBox> OcrEngine::recognize(const QImage& image,
                                      const std::function<bool()>& cancelled,
                                      const ProgressFn& progress,
                                      int detLimitSideOverride)
{
    m_cancelled = cancelled;
    if (!isLoaded() || image.isNull())
        return {};

    const qint64 startedAt = ocrMonotonicMs();
    m_impl->lastTiming = OcrTimings();

    const QImage input = m_impl->binarize ? binarizeImage(image) : image;

    // Anh lon (full window/game UI): det model resize ve ~960px lam mat chu
    // nho. Chia 2x2 co overlap de bao phu cac label/tab ma luot full bo sot.
    const qint64 area = qint64(input.width()) * qint64(input.height());
    const bool tile = qEnvironmentVariable("EZ_OCR_TILE", QStringLiteral("1")).trimmed()
                          != QStringLiteral("0")
                      && input.width() >= 1600 && input.height() >= 900
                      && area >= 1500000;
    if (!tile) {
        const QList<OcrTextBox> result = recognizeProcessed(
            input, true, progress, detLimitSideOverride);
        m_impl->lastTiming.boxes = result.size();
        m_impl->lastTiming.totalMs = double(ocrMonotonicMs() - startedAt);
        return result;
    }

    const int cols = 2;
    const int rows = 2;
    const int overlapX = std::max(24, input.width() / 14);
    const int overlapY = std::max(18, input.height() / 14);
    QList<OcrTextBox> merged;

    // Thí nghiệm detect-once: detector vẫn chạy theo tile để giữ recall, nhưng recognition
    // chạy sau khi merge trên ảnh gốc. Bật/tắt được bằng biến môi trường để A/B accuracy và p95.
    const bool detectOnce = qEnvironmentVariable("EZ_OCR_TILE_DETECT_ONCE", QStringLiteral("0"))
                                .trimmed()
                            != QStringLiteral("0");
    if (detectOnce) {
        for (int row = 0; row < rows; ++row) {
            const int y0 = std::max(0, row * input.height() / rows - (row > 0 ? overlapY : 0));
            const int y1 = std::min(input.height(), (row + 1) * input.height() / rows
                                                        + (row + 1 < rows ? overlapY : 0));
            for (int col = 0; col < cols; ++col) {
                if (m_cancelled && m_cancelled())
                    return {};
                const int x0 = std::max(0, col * input.width() / cols - (col > 0 ? overlapX : 0));
                const int x1 = std::min(input.width(), (col + 1) * input.width() / cols
                                                          + (col + 1 < cols ? overlapX : 0));
                const QRect tileRect(x0, y0, x1 - x0, y1 - y0);
                if (tileRect.width() < 8 || tileRect.height() < 8)
                    continue;
                const QList<OcrTextBox> boxes =
                    m_impl->detector.detect(input.copy(tileRect),
                                            detLimitSideOverride > 0
                                                ? detLimitSideOverride
                                                : m_impl->options.detLimitSide);
                ++m_impl->lastTiming.detCalls;
                ++m_impl->lastTiming.tileCount;
                m_impl->lastTiming.detMs += m_impl->detector.lastMs();
                for (OcrTextBox box : boxes) {
                    box.rect.translate(tileRect.topLeft());
                    merged.append(box);
                }
            }
        }

        // Merge box trùng ở vùng overlap trước khi crop recognition, tránh đọc cùng một dòng
        // nhiều lần và bảo đảm recognizer nhìn thấy tọa độ trên ảnh gốc.
        std::sort(merged.begin(), merged.end(), [](const OcrTextBox& a, const OcrTextBox& b) {
            const int dy = a.rect.center().y() - b.rect.center().y();
            if (std::abs(dy) > 6)
                return dy < 0;
            return a.rect.left() < b.rect.left();
        });
        QList<OcrTextBox> detected;
        for (const OcrTextBox& box : merged) {
            bool duplicate = false;
            for (OcrTextBox& existing : detected) {
                const QRect intersection = box.rect.intersected(existing.rect);
                const QRect united = box.rect.united(existing.rect);
                const double unionArea = double(united.width()) * double(united.height());
                const double iou = unionArea > 0.0
                                       ? double(intersection.width()) * double(intersection.height())
                                             / unionArea
                                       : 0.0;
                const QPoint delta = box.rect.center() - existing.rect.center();
                if (iou > 0.20 || (std::abs(delta.y()) < 8 && std::abs(delta.x()) < 34)) {
                    existing.rect = existing.rect.united(box.rect);
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate)
                detected.append(box);
        }
        qInfo() << "[OCR] tile detect-once boxes=" << detected.size();
        const QList<OcrTextBox> result = recognizeBoxes(input, detected, true);
        m_impl->lastTiming.boxes = result.size();
        m_impl->lastTiming.totalMs = double(ocrMonotonicMs() - startedAt);
        return result;
    }

    for (int row = 0; row < rows; ++row) {
        const int y0 = std::max(0, row * input.height() / rows - (row > 0 ? overlapY : 0));
        const int y1 = std::min(input.height(), (row + 1) * input.height() / rows
                                                    + (row + 1 < rows ? overlapY : 0));
        for (int col = 0; col < cols; ++col) {
            const int x0 = std::max(0, col * input.width() / cols - (col > 0 ? overlapX : 0));
            const int x1 = std::min(input.width(), (col + 1) * input.width() / cols
                                                      + (col + 1 < cols ? overlapX : 0));
            const QRect tileRect(x0, y0, x1 - x0, y1 - y0);
            if (tileRect.width() < 8 || tileRect.height() < 8)
                continue;
            const QList<OcrTextBox> boxes =
                recognizeProcessed(input.copy(tileRect), false, {}, detLimitSideOverride);
            for (OcrTextBox box : boxes) {
                box.rect.translate(tileRect.topLeft());
                merged.append(box);
            }
        }
    }

    // Dedupe boxes across tile borders. Hai tile co the doc cung mot vung
    // thanh 2 text hoi khac nhau; uu tien box dai hon/conf cao hon.
    std::sort(merged.begin(), merged.end(), [](const OcrTextBox& a, const OcrTextBox& b) {
        const int dy = a.rect.center().y() - b.rect.center().y();
        if (std::abs(dy) > 6)
            return dy < 0;
        return a.rect.left() < b.rect.left();
    });
    QList<OcrTextBox> result;
    for (const OcrTextBox& box : merged) {
        bool duplicate = false;
        for (OcrTextBox& existing : result) {
            const QRect intersection = box.rect.intersected(existing.rect);
            const QRect united = box.rect.united(existing.rect);
            const double unionArea = double(united.width()) * double(united.height());
            const double iou = unionArea > 0.0
                                   ? double(intersection.width()) * double(intersection.height())
                                         / unionArea
                                   : 0.0;
            const QPoint delta = box.rect.center() - existing.rect.center();
            if (iou > 0.20 || (std::abs(delta.y()) < 8 && std::abs(delta.x()) < 34)) {
                // Tiling can split one long source line into two overlapping
                // detections (left tile + right tile). Dropping one leaves the
                // other half of the line uncovered, so the original glyphs
                // remain visible. Union the rects and keep the longer text.
                const bool longerText = box.text.size() > existing.text.size();
                const QString mergedText = longerText ? box.text : existing.text;
                const float mergedConfidence = std::max(existing.confidence, box.confidence);
                existing.rect = existing.rect.united(box.rect);
                existing.text = mergedText;
                existing.confidence = mergedConfidence;
                duplicate = true;
                break;
            }
        }
        if (!duplicate)
            result.append(box);
    }

    m_impl->lastBoxes = result.size();
    m_impl->lastTiming.boxes = result.size();
    m_impl->lastTiming.totalMs = double(ocrMonotonicMs() - startedAt);
    return result;
}

QList<OcrTextBox> OcrEngine::recognizeProcessed(const QImage& input, bool allowAlt,
                                               const ProgressFn& progress,
                                               int detLimitSideOverride)
{
    if (!isLoaded() || input.isNull() || (m_cancelled && m_cancelled()))
        return {};

    const QList<OcrTextBox> boxes = m_impl->detector.detect(input, detLimitSideOverride);
    ++m_impl->lastTiming.detCalls;
    ++m_impl->lastTiming.tileCount;
    m_impl->lastTiming.detMs += m_impl->detector.lastMs();
    m_impl->lastBoxes = boxes.size();
    if (boxes.isEmpty() || (m_cancelled && m_cancelled()))
        return {};

    // CPU full-page: nhận dạng theo cụm để lớp ứng dụng có thể dịch/hiện cụm đầu
    // tiên trong lúc các dòng còn lại vẫn đang chạy. GPU đã batch cả trang nên
    // không cần chia; chia thêm chỉ làm tăng dispatch.
    const bool progressive = progress
                             && m_impl->recRuntime.providerName() == QLatin1String("CPU")
                             && boxes.size() >= 8
                             && qEnvironmentVariable("EZ_OCR_PROGRESSIVE", QStringLiteral("1"))
                                    != QLatin1String("0");
    if (progressive) {
        int chunk = qEnvironmentVariableIntValue("EZ_OCR_PROGRESSIVE_CHUNK");
        if (chunk <= 0) {
            // Cụm đầu bằng số worker: máy yếu (2 worker) có kết quả sớm hơn,
            // máy nhiều core (6 worker) ít lần chia lại hơn.
            const int workers = qMax(2, m_impl->options.recWorkers);
            chunk = qBound(2, workers, 6);
        }
        chunk = qBound(2, chunk, 12);

        QList<OcrTextBox> all;
        all.reserve(boxes.size());
        for (int start = 0; start < boxes.size(); start += chunk) {
            if (m_cancelled && m_cancelled())
                return {};
            const QList<OcrTextBox> part =
                recognizeBoxes(input, boxes.mid(start, chunk), allowAlt);
            all.append(part);
            if (progress && !all.isEmpty())
                progress(all);
        }
        return all;
    }

    return recognizeBoxes(input, boxes, allowAlt);
}

QList<OcrTextBox> OcrEngine::recognizeBoxes(const QImage& input,
                                            const QList<OcrTextBox>& boxes, bool allowAlt)
{
    if (boxes.isEmpty())
        return {};

    // MODEL CHỮ THEO NỘI DUNG: ngôn ngữ nguồn trong Cài đặt có thể LỆCH với nội dung thật (đặt
    // "en" cho trang truyện rồi mở cửa sổ tiếng Trung). Model sai từ điển vẫn trả chữ nhưng là
    // RÁC với độ tin cậy thấp ⇒ bản dịch bị lọc bỏ ⇒ "không phản ứng gì" (cả overlay lẫn hover).
    // Điểm tin cậy thấp thì thử model hệ chữ còn lại, chọn bên đọc tốt hơn — và NHỚ bên thắng để
    // các khung sau chỉ chạy MỘT model (nội dung không đổi ngôn ngữ giữa chừng).
    if (allowAlt && m_impl->altLoaded && m_impl->preferAlt) {
        const QList<OcrTextBox> alt = m_impl->altRecognizer.recognize(input, boxes, m_cancelled);
        addRecognizerTiming(m_impl->lastTiming, m_impl->altRecognizer.lastTimings(), true);
        if (m_cancelled && m_cancelled())
            return {};
        if (meanConfidence(alt) >= kLowConfidence)
            return alt; // model dự phòng vẫn đọc tốt → dùng luôn
        m_impl->preferAlt = false; // đọc kém (đã đổi nội dung) → so sánh lại đầy đủ
    }

    const QList<OcrTextBox> primary = m_impl->recognizer.recognize(input, boxes, m_cancelled);
    addRecognizerTiming(m_impl->lastTiming, m_impl->recognizer.lastTimings(), false);
    if (m_cancelled && m_cancelled())
        return {};
    const double primaryConfidence = meanConfidence(primary);
    if (allowAlt && primaryConfidence < kLowConfidence && ensureAltRecognizer()) {
        const QList<OcrTextBox> alt = m_impl->altRecognizer.recognize(input, boxes, m_cancelled);
        addRecognizerTiming(m_impl->lastTiming, m_impl->altRecognizer.lastTimings(), true);
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
    if (!m_impl->altRecognizer.load(m_impl->recRuntime, alt, &error)) {
        qWarning() << "[OCR] model chữ dự phòng nạp thất bại:" << error;
        return false;
    }
    m_impl->altLoaded = true;
    qInfo() << "[OCR] đã nạp model chữ dự phòng:" << QFileInfo(alt.recModelPath).fileName();
    return true;
}

OcrTimings OcrEngine::lastTimings() const
{
    return m_impl ? m_impl->lastTiming : OcrTimings();
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
