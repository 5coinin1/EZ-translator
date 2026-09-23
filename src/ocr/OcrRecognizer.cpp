#include "ocr/OcrRecognizer.h"
#include "ocr/OcrRuntime.h"

#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQueue>
#include <QStringConverter>
#include <QTextStream>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

namespace EZTranslator {

namespace {

constexpr int kRecHeight = 48;

using Clock = std::chrono::steady_clock;

double msSince(const Clock::time_point& start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

/** CTC greedy decode của block logits [steps, classes] thành text. */
QString decodeLogits(const float* logits, int steps, int classes,
                     const std::vector<QString>& dictionary, float* confidenceOut)
{
    QString text;
    float confidence = 0.0f;
    int counted = 0;
    int previous = -1;
    const bool hasSpaceClass = (classes == int(dictionary.size()) + 2);

    for (int t = 0; t < steps; ++t) {
        const float* row = logits + size_t(t) * size_t(classes);
        int best = 0;
        float bestValue = row[0];
        for (int c = 1; c < classes; ++c) {
            if (row[c] > bestValue) {
                bestValue = row[c];
                best = c;
            }
        }
        // 0 = blank; bỏ ký tự lặp liên tiếp (CTC collapse).
        if (best > 0 && best != previous) {
            const int dictIndex = best - 1;
            if (dictIndex < int(dictionary.size()))
                text += dictionary[dictIndex];
            else if (hasSpaceClass)
                text += QLatin1Char(' ');
            confidence += bestValue;
            ++counted;
        }
        previous = best;
    }

    if (confidenceOut)
        *confidenceOut = counted > 0 ? confidence / counted : 0.0f;
    return text;
}

/** Hash nội dung ảnh (dùng làm key cache dòng): 16x16 grayscale + FNV-1a. */
quint64 imageContentHash(const QImage& image)
{
    if (image.isNull())
        return 0;
    const QImage small = image.convertToFormat(QImage::Format_Grayscale8)
                             .scaled(16, 16, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    quint64 hash = 1469598103934665603ull;
    for (int y = 0; y < small.height(); ++y) {
        const uchar* line = small.constScanLine(y);
        for (int x = 0; x < small.width(); ++x) {
            hash ^= line[x];
            hash *= 1099511628211ull;
        }
    }
    return hash;
}

} // namespace

struct OcrRecognizer::Impl
{
    OcrOptions options;
    std::shared_ptr<OcrSession> session;
    std::vector<QString> dictionary;
    QString providerName;
    bool loaded{false};
    bool dynamicBatch{false};
    OcrTimings timings;

    /** Cache kết quả rec theo nội dung dòng (LRU thủ công để lưu/đọc được). */
    struct LineCache
    {
        int limit{4096};
        QHash<quint64, OcrTextBox> map;
        QQueue<quint64> order;

        [[nodiscard]] const OcrTextBox* get(quint64 key) const
        {
            const auto it = map.constFind(key);
            return it == map.constEnd() ? nullptr : &it.value();
        }
        void insert(quint64 key, const OcrTextBox& value)
        {
            if (!map.contains(key))
                order.enqueue(key);
            map.insert(key, value);
            while (map.size() > limit && !order.isEmpty())
                map.remove(order.dequeue());
        }
        void clear()
        {
            map.clear();
            order.clear();
        }
    };
    LineCache lineCache;
};

OcrRecognizer::OcrRecognizer()
    : m_impl(std::make_unique<Impl>())
{
}

OcrRecognizer::~OcrRecognizer() = default;

bool OcrRecognizer::isLoaded() const
{
    return m_impl && m_impl->loaded;
}

OcrTimings OcrRecognizer::lastTimings() const
{
    return m_impl ? m_impl->timings : OcrTimings();
}

bool OcrRecognizer::load(OcrRuntime& runtime, const OcrOptions& options, QString* error)
{
    if (!m_impl)
        return false;

    m_impl->options = options;
    m_impl->providerName = runtime.providerName();
    m_impl->lineCache.limit = std::max(0, options.lineCacheLimit);

    // fp16 chỉ áp cho rec: nhanh hơn ~15% trên GPU, và box không bị ảnh hưởng.
    if (options.useGpu) {
        QString base = options.recModelPath;
        if (base.endsWith(QStringLiteral(".onnx"), Qt::CaseInsensitive))
            base.chop(5);
        const QString fp16 = base + QStringLiteral("_fp16.onnx");
        if (QFile::exists(fp16))
            m_impl->options.recModelPath = fp16;
    }

    QFile dictFile(m_impl->options.dictionaryPath);
    if (!dictFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = QStringLiteral("Không mở được dictionary: ") + m_impl->options.dictionaryPath;
        return false;
    }
    QTextStream stream(&dictFile);
    stream.setEncoding(QStringConverter::Utf8);
    while (!stream.atEnd()) {
        QString line = stream.readLine();
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);
        m_impl->dictionary.push_back(line);
    }

    m_impl->session = runtime.createSession(m_impl->options.recModelPath,
                                            m_impl->options.recThreads, true, error);
    if (!m_impl->session)
        return false;

    m_impl->dynamicBatch = m_impl->session->dynamicBatch();
    m_impl->loaded = true;
    return true;
}

QList<OcrTextBox> OcrRecognizer::recognize(const QImage& image, const QList<OcrTextBox>& boxes)
{
    QList<OcrTextBox> result;
    if (!isLoaded() || image.isNull() || boxes.isEmpty())
        return result;

    Impl& impl = *m_impl;
    const QImage rgb = image.convertToFormat(QImage::Format_RGB32);

    struct RecItem
    {
        OcrTextBox box;
        int width{0};
        QImage image;
        int boxIndex{0};
        int order{0};
    };

    // Dòng dài bị chia thành nhiều đoạn chồng lấn (cost của rec head tăng ~bậc 2
    // theo bề rộng), rồi ghép text lại.
    const int recMaxWidth = std::max(64, impl.options.recMaxWidth);

    std::vector<RecItem> items;
    items.reserve(size_t(boxes.size()));
    for (int boxIndex = 0; boxIndex < boxes.size(); ++boxIndex) {
        const OcrTextBox& box = boxes[boxIndex];
        const QRect cropRect = box.rect.adjusted(-2, -2, 2, 2).intersected(rgb.rect());
        if (cropRect.width() < 2 || cropRect.height() < 2)
            continue;

        const QImage crop = rgb.copy(cropRect);
        const int cropWidth = crop.width();
        const int fullWidth =
            std::max(16, int(std::lround(double(cropWidth) * kRecHeight / crop.height())));
        const int segments = std::max(1, (fullWidth + recMaxWidth - 1) / recMaxWidth);
        const int segmentWidth = (cropWidth + segments - 1) / segments;
        const int overlap = segments > 1 ? std::max(2, segmentWidth / 12) : 0;

        for (int segment = 0; segment < segments; ++segment) {
            const int x0 = std::max(0, segment * segmentWidth - overlap);
            const int x1 = std::min(cropWidth, (segment + 1) * segmentWidth + overlap);
            if (x1 - x0 < 2)
                continue;

            const QImage piece = crop.copy(x0, 0, x1 - x0, crop.height());
            const int targetWidth = std::clamp(
                int(std::lround(double(piece.width()) * kRecHeight / piece.height())), 16,
                recMaxWidth);

            RecItem item;
            item.box = box;
            item.width = targetWidth;
            item.image = piece.scaled(targetWidth, kRecHeight, Qt::IgnoreAspectRatio,
                                      Qt::SmoothTransformation);
            item.boxIndex = boxIndex;
            item.order = segment;
            items.push_back(std::move(item));
        }
    }

    // Cache theo nội dung: dòng không đổi dùng lại text cũ.
    std::vector<quint64> keys(items.size(), 0);
    std::vector<OcrTextBox> outputs(items.size());
    std::vector<char> have(items.size(), 0);
    std::vector<size_t> pending;
    for (size_t i = 0; i < items.size(); ++i) {
        outputs[i] = items[i].box;
        keys[i] = imageContentHash(items[i].image);
        if (const OcrTextBox* cached = impl.lineCache.get(keys[i])) {
            outputs[i].text = cached->text;
            outputs[i].confidence = cached->confidence;
            have[i] = 1;
            ++impl.timings.recCached;
        } else {
            pending.push_back(i);
        }
    }

    const auto fillSample = [](std::vector<float>& data, size_t sampleOffset, size_t plane,
                               int width, int rowStride, const QImage& sample) {
        for (int y = 0; y < kRecHeight; ++y) {
            const QRgb* line = reinterpret_cast<const QRgb*>(sample.constScanLine(y));
            const size_t rowOffset = size_t(y) * rowStride;
            for (int x = 0; x < width; ++x) {
                const QRgb pixel = line[x];
                data[sampleOffset + rowOffset + x] = (qRed(pixel) / 255.0f - 0.5f) / 0.5f;
                data[sampleOffset + plane + rowOffset + x] =
                    (qGreen(pixel) / 255.0f - 0.5f) / 0.5f;
                data[sampleOffset + 2 * plane + rowOffset + x] =
                    (qBlue(pixel) / 255.0f - 0.5f) / 0.5f;
            }
        }
    };

    // Một forward pass phục vụ cả batch (pad về bề rộng lớn nhất trong batch).
    const auto runRecBatch = [&](const std::vector<RecItem>& batch,
                                 std::vector<std::pair<QString, float>>& decoded) -> bool {
        int maxWidth = 0;
        for (const RecItem& item : batch)
            maxWidth = std::max(maxWidth, item.width);
        const int n = int(batch.size());
        const size_t plane = size_t(kRecHeight) * size_t(maxWidth);
        std::vector<float> data(size_t(n) * 3 * plane, 0.0f);
        for (int i = 0; i < n; ++i)
            fillSample(data, size_t(i) * 3 * plane, plane, batch[i].width, maxWidth, batch[i].image);

        const std::vector<int64_t> shape = {n, 3, kRecHeight, maxWidth};
        const auto runStart = Clock::now();
        const bool ok = impl.session->run(
            data.data(), shape, [&](const float* logits, const std::vector<int64_t>& outShape) {
                if (outShape.size() != 3)
                    return;
                const int steps = int(outShape[1]);
                const int classes = int(outShape[2]);
                impl.timings.recOutputBytes += qint64(n) * steps * classes * 4;

                const auto decodeStart = Clock::now();
                decoded.assign(batch.size(), {QString(), 0.0f});
                for (int i = 0; i < n; ++i) {
                    float confidence = 0.0f;
                    const QString text = decodeLogits(logits + size_t(i) * size_t(steps) * size_t(classes),
                                                      steps, classes, impl.dictionary, &confidence);
                    decoded[i] = {text, confidence};
                }
                impl.timings.recDecodeMs += msSince(decodeStart);
            });
        if (!ok)
            return false;

        impl.timings.recRunMs += msSince(runStart);
        ++impl.timings.recCalls;
        return true;
    };

    const bool gpuProvider = impl.providerName == QLatin1String("DirectML")
                             || impl.providerName == QLatin1String("CUDA");
    // Batch chỉ lợi trên GPU (một Run gộp nhiều dòng); trên CPU nó chậm hơn nên
    // mặc định tắt và chạy các dòng song song thay vì gộp.
    const bool batchEnabled = !impl.options.disableRecBatch || gpuProvider;
    const bool batch = impl.dynamicBatch && pending.size() > 1 && batchEnabled;
    const size_t chunk = batch ? size_t(gpuProvider ? 48 : 8) : 1;
    impl.timings.recBatched = batch;

    // Ghép các đoạn chồng lấn trở lại thành text của từng box.
    const auto mergeOverlap = [](const QString& accumulated, const QString& next) {
        if (accumulated.isEmpty() || next.isEmpty())
            return accumulated + next;
        const int maxOverlap = std::min({int(accumulated.size()), int(next.size()), 32});
        for (int k = maxOverlap; k >= 3; --k) {
            if (accumulated.right(k) == next.left(k))
                return accumulated + next.mid(k);
        }
        return accumulated + next;
    };

    const auto assembleResult = [&]() {
        QList<OcrTextBox> out;
        int currentBox = -1;
        QString text;
        float confidence = 0.0f;
        int counted = 0;
        const auto flush = [&]() {
            if (currentBox >= 0 && !text.isEmpty()) {
                OcrTextBox box = boxes[currentBox];
                box.text = text;
                box.confidence = counted > 0 ? confidence / counted : 0.0f;
                out.append(box);
            }
            text.clear();
            confidence = 0.0f;
            counted = 0;
        };
        for (size_t i = 0; i < items.size(); ++i) {
            if (!have[i])
                continue;
            if (items[i].boxIndex != currentBox) {
                flush();
                currentBox = items[i].boxIndex;
            }
            if (!outputs[i].text.isEmpty()) {
                text = mergeOverlap(text, outputs[i].text);
                confidence += outputs[i].confidence;
                ++counted;
            }
        }
        flush();
        return out;
    };

    // Recognition MỘT dòng (đường song song trên CPU).
    const auto recOne = [&](const RecItem& item, QString* text, float* confidence) -> bool {
        const int width = item.width;
        const size_t plane = size_t(kRecHeight) * size_t(width);
        std::vector<float> data(size_t(3) * plane, 0.0f);
        fillSample(data, 0, plane, width, width, item.image);
        const std::vector<int64_t> shape = {1, 3, kRecHeight, width};
        return impl.session->run(
            data.data(), shape, [&](const float* logits, const std::vector<int64_t>& outShape) {
                if (outShape.size() != 3)
                    return;
                *text = decodeLogits(logits, int(outShape[1]), int(outShape[2]), impl.dictionary,
                                     confidence);
            });
    };

    const auto recStart = Clock::now();

    // CPU: không batch thì fan các dòng ra vài worker (ORT session an toàn với Run
    // đồng thời trên CPU). GPU: đi đường batch phía dưới (DirectML không cho Run song song).
    int recWorkers = 0;
    if (!gpuProvider) {
        const unsigned hw = std::thread::hardware_concurrency();
        recWorkers = int(std::max(2u, std::min(6u, hw / 2)));
    }
    if (!batch && recWorkers > 1 && pending.size() >= 2) {
        std::atomic<size_t> cursor{0};
        std::vector<std::thread> pool;
        pool.reserve(size_t(recWorkers));
        for (int worker = 0; worker < recWorkers; ++worker) {
            pool.emplace_back([&]() {
                for (;;) {
                    const size_t j = cursor.fetch_add(1, std::memory_order_relaxed);
                    if (j >= pending.size())
                        break;
                    const size_t index = pending[j];
                    QString text;
                    float confidence = 0.0f;
                    if (recOne(items[index], &text, &confidence)) {
                        outputs[index].text = text;
                        outputs[index].confidence = confidence;
                        have[index] = 1;
                        impl.lineCache.insert(keys[index], outputs[index]);
                    }
                }
            });
        }
        for (std::thread& worker : pool)
            worker.join();
        impl.timings.recCalls += int(pending.size());
        impl.timings.recMs = msSince(recStart);
        return assembleResult();
    }

    if (batch) {
        // Pad về bề rộng lớn nhất batch, nên gộp dòng ngắn với dòng dài rất phí.
        // Sắp theo bề rộng để giảm lãng phí padding.
        std::stable_sort(pending.begin(), pending.end(), [&](size_t a, size_t b) {
            return items[a].width < items[b].width;
        });
    }
    for (size_t start = 0; start < pending.size();) {
        size_t end = start + 1;
        if (batch) {
            const int baseWidth = items[pending[start]].width;
            while (end < pending.size() && end - start < chunk) {
                const int width = items[pending[end]].width;
                // Ngừng mở rộng batch khi padding lãng phí vượt ~30%.
                if (baseWidth > 0 && width > int(baseWidth * 1.3))
                    break;
                ++end;
            }
        } else {
            end = std::min(pending.size(), start + chunk);
        }

        std::vector<RecItem> sub;
        sub.reserve(end - start);
        for (size_t j = start; j < end; ++j)
            sub.push_back(items[pending[j]]);

        std::vector<std::pair<QString, float>> decoded;
        if (!runRecBatch(sub, decoded)) {
            // Fallback: một call mỗi dòng.
            for (size_t j = start; j < end; ++j) {
                std::vector<std::pair<QString, float>> single;
                const bool ok = runRecBatch(std::vector<RecItem>{items[pending[j]]}, single);
                const size_t idx = pending[j];
                if (ok && !single.empty()) {
                    outputs[idx].text = single[0].first;
                    outputs[idx].confidence = single[0].second;
                }
                have[idx] = 1;
                impl.lineCache.insert(keys[idx], outputs[idx]);
            }
            start = end;
            continue;
        }

        for (size_t j = start; j < end; ++j) {
            const size_t idx = pending[j];
            const size_t local = j - start;
            if (local < decoded.size()) {
                outputs[idx].text = decoded[local].first;
                outputs[idx].confidence = decoded[local].second;
            }
            have[idx] = 1;
            impl.lineCache.insert(keys[idx], outputs[idx]);
        }
        start = end;
    }
    impl.timings.recMs = msSince(recStart);
    return assembleResult();
}

int OcrRecognizer::lineCacheSize() const
{
    return m_impl ? m_impl->lineCache.map.size() : 0;
}

void OcrRecognizer::clearLineCache()
{
    if (m_impl)
        m_impl->lineCache.clear();
}

bool OcrRecognizer::saveLineCache(const QString& path) const
{
    if (!m_impl)
        return false;

    QJsonObject object;
    for (auto it = m_impl->lineCache.map.constBegin(); it != m_impl->lineCache.map.constEnd(); ++it) {
        QJsonObject entry;
        entry.insert(QStringLiteral("t"), it.value().text);
        entry.insert(QStringLiteral("c"), double(it.value().confidence));
        object.insert(QString::number(it.key()), entry);
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
    return true;
}

bool OcrRecognizer::loadLineCache(const QString& path)
{
    if (!m_impl)
        return false;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
    for (auto it = object.begin(); it != object.end(); ++it) {
        bool ok = false;
        const quint64 key = it.key().toULongLong(&ok);
        if (!ok)
            continue;
        const QJsonObject entry = it.value().toObject();
        OcrTextBox box;
        box.text = entry.value(QStringLiteral("t")).toString();
        box.confidence = float(entry.value(QStringLiteral("c")).toDouble());
        if (box.text.isEmpty())
            continue;
        m_impl->lineCache.insert(key, box);
    }
    return true;
}

} // namespace EZTranslator
