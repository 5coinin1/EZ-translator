#include "ocr/OcrDetector.h"
#include "ocr/OcrRuntime.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

namespace EZTranslator {

namespace {

constexpr float kNormalizeMean[3] = {0.485f, 0.456f, 0.406f};
constexpr float kNormalizeStd[3]  = {0.229f, 0.224f, 0.225f};

struct Component
{
    int minX{0};
    int minY{0};
    int maxX{0};
    int maxY{0};
    int area{0};
};

using Clock = std::chrono::steady_clock;

double msSince(const Clock::time_point& start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

/** Connected components 8 hướng trên mask nhị phân. */
std::vector<Component> labelComponents(const std::vector<quint8>& mask, int width, int height)
{
    std::vector<int> label(size_t(width) * size_t(height), -1);
    std::vector<Component> components;
    std::vector<int> stack;

    for (int index = 0; index < width * height; ++index) {
        if (!mask[index] || label[index] >= 0)
            continue;

        Component component;
        component.minX = component.maxX = index % width;
        component.minY = component.maxY = index / width;

        const int id = int(components.size());
        label[index] = id;
        stack.clear();
        stack.push_back(index);

        while (!stack.empty()) {
            const int point = stack.back();
            stack.pop_back();
            const int px = point % width;
            const int py = point / width;

            ++component.area;
            component.minX = std::min(component.minX, px);
            component.maxX = std::max(component.maxX, px);
            component.minY = std::min(component.minY, py);
            component.maxY = std::max(component.maxY, py);

            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0)
                        continue;
                    const int nx = px + dx;
                    const int ny = py + dy;
                    if (nx < 0 || ny < 0 || nx >= width || ny >= height)
                        continue;
                    const int neighbour = ny * width + nx;
                    if (mask[neighbour] && label[neighbour] < 0) {
                        label[neighbour] = id;
                        stack.push_back(neighbour);
                    }
                }
            }
        }
        components.push_back(component);
    }
    return components;
}

} // namespace

struct OcrDetector::Impl
{
    OcrOptions options;
    std::shared_ptr<OcrSession> session;
    bool loaded{false};
    double lastMs{0.0};
};

OcrDetector::OcrDetector()
    : m_impl(std::make_unique<Impl>())
{
}

OcrDetector::~OcrDetector() = default;

bool OcrDetector::isLoaded() const
{
    return m_impl && m_impl->loaded;
}

double OcrDetector::lastMs() const
{
    return m_impl ? m_impl->lastMs : 0.0;
}

bool OcrDetector::load(OcrRuntime& runtime, const OcrOptions& options, QString* error)
{
    if (!m_impl)
        return false;

    m_impl->options = options;
    // Detector là một run mỗi frame → vài intra-op thread giúp ích.
    m_impl->session = runtime.createSession(options.detModelPath, options.threads, true, error);
    m_impl->loaded = m_impl->session != nullptr;
    return m_impl->loaded;
}

QList<OcrTextBox> OcrDetector::detect(const QImage& image)
{
    QList<OcrTextBox> boxes;
    if (!isLoaded() || image.isNull())
        return boxes;

    Impl& impl = *m_impl;
    const QImage rgb = image.convertToFormat(QImage::Format_RGB32);
    const int originalWidth = rgb.width();
    const int originalHeight = rgb.height();

    // Resize cạnh dài ≤ detLimitSide, làm tròn lên bội số 32.
    const int detLimitSide = std::max(160, impl.options.detLimitSide);
    const int maxSide = std::max(originalWidth, originalHeight);
    const double scale = maxSide > detLimitSide ? double(detLimitSide) / maxSide : 1.0;
    const int resizedWidth = std::max(32, int(std::ceil(originalWidth * scale / 32.0)) * 32);
    const int resizedHeight = std::max(32, int(std::ceil(originalHeight * scale / 32.0)) * 32);

    const QImage detInput =
        rgb.scaled(resizedWidth, resizedHeight, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    // CHW, normalize theo mean/std của ImageNet.
    const int plane = resizedWidth * resizedHeight;
    std::vector<float> detData(size_t(plane) * 3);
    for (int y = 0; y < resizedHeight; ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(detInput.constScanLine(y));
        const size_t rowOffset = size_t(y) * resizedWidth;
        for (int x = 0; x < resizedWidth; ++x) {
            const QRgb pixel = line[x];
            detData[rowOffset + x] =
                (qRed(pixel) / 255.0f - kNormalizeMean[0]) / kNormalizeStd[0];
            detData[plane + rowOffset + x] =
                (qGreen(pixel) / 255.0f - kNormalizeMean[1]) / kNormalizeStd[1];
            detData[size_t(2) * plane + rowOffset + x] =
                (qBlue(pixel) / 255.0f - kNormalizeMean[2]) / kNormalizeStd[2];
        }
    }

    const std::vector<int64_t> detShape = {1, 3, resizedHeight, resizedWidth};
    const auto detStart = Clock::now();

    const bool ok = impl.session->run(
        detData.data(), detShape, [&](const float* probability, const std::vector<int64_t>& shape) {
            if (shape.size() != 4)
                return;
            const int probWidth = int(shape[3]);
            const int probHeight = int(shape[2]);

            std::vector<quint8> mask(size_t(probWidth) * size_t(probHeight));
            for (int i = 0; i < probWidth * probHeight; ++i)
                mask[i] = probability[i] > impl.options.detThreshold ? 1 : 0;

            const std::vector<Component> components = labelComponents(mask, probWidth, probHeight);

            const double scaleBackX = double(originalWidth) / probWidth;
            const double scaleBackY = double(originalHeight) / probHeight;

            for (const Component& component : components) {
                const int width = component.maxX - component.minX + 1;
                const int height = component.maxY - component.minY + 1;
                if (width < 3 || height < 3 || component.area < 8)
                    continue;

                // Unclip: nở box theo diện tích/chu vi (text thật to hơn vùng mực).
                const double perimeter = 2.0 * (width + height);
                const double distance = perimeter > 0.0
                                            ? component.area * impl.options.unclipRatio / perimeter
                                            : 0.0;

                QRect box(int(std::lround(component.minX - distance)),
                          int(std::lround(component.minY - distance)),
                          int(std::lround(width + 2.0 * distance)),
                          int(std::lround(height + 2.0 * distance)));
                box = QRect(int(std::lround(box.left() * scaleBackX)),
                            int(std::lround(box.top() * scaleBackY)),
                            int(std::lround(box.width() * scaleBackX)),
                            int(std::lround(box.height() * scaleBackY)));
                box = box.intersected(QRect(0, 0, originalWidth, originalHeight));
                if (box.width() < 3 || box.height() < 3)
                    continue;

                OcrTextBox textBox;
                textBox.rect = box;
                boxes.append(textBox);
            }
        });

    impl.lastMs = msSince(detStart);
    if (!ok)
        return {};

    std::sort(boxes.begin(), boxes.end(), [](const OcrTextBox& a, const OcrTextBox& b) {
        if (std::abs(a.rect.top() - b.rect.top()) > 8)
            return a.rect.top() < b.rect.top();
        return a.rect.left() < b.rect.left();
    });
    return boxes;
}

} // namespace EZTranslator
