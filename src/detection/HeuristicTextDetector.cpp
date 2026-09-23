#include "detection/HeuristicTextDetector.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace EZTranslator {
namespace {

struct Component
{
    int minX{0};
    int minY{0};
    int maxX{0};
    int maxY{0};
    int area{0};
};

/** Ảnh xám đã downscale kèm hệ số scale để map ngược về ảnh gốc. */
struct WorkingImage
{
    QImage image;
    double scale{1.0};
};

WorkingImage makeWorkingImage(const QImage& source, int maxDimension)
{
    const QImage gray = source.convertToFormat(QImage::Format_Grayscale8);

    WorkingImage work;
    work.image = gray;

    const int longest = std::max(gray.width(), gray.height());
    if (maxDimension > 0 && longest > maxDimension) {
        work.scale = double(maxDimension) / double(longest);
        work.image = gray.scaled(int(gray.width() * work.scale),
                                 int(gray.height() * work.scale),
                                 Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return work;
}

/** integral[(y+1)*stride + (x+1)] = tổng pixel trong [0..x] x [0..y]. */
std::vector<quint64> buildIntegral(const QImage& gray)
{
    const int width  = gray.width();
    const int height = gray.height();
    const int stride = width + 1;

    std::vector<quint64> integral(size_t(stride) * size_t(height + 1), 0);
    for (int y = 0; y < height; ++y) {
        const uchar* line = gray.constScanLine(y);
        quint64 rowSum = 0;
        for (int x = 0; x < width; ++x) {
            rowSum += line[x];
            integral[size_t(y + 1) * stride + (x + 1)] =
                integral[size_t(y) * stride + (x + 1)] + rowSum;
        }
    }
    return integral;
}

/** Đánh dấu pixel "mực" bằng ngưỡng thích nghi theo mean cục bộ. */
std::vector<quint8> buildInkMask(const QImage& gray,
                                 const std::vector<quint64>& integral,
                                 int window, int threshold)
{
    const int width  = gray.width();
    const int height = gray.height();
    const int stride = width + 1;

    const auto rectSum = [&](int x0, int y0, int x1, int y1) -> quint64 {
        return integral[size_t(y1) * stride + x1] - integral[size_t(y0) * stride + x1]
             - integral[size_t(y1) * stride + x0] + integral[size_t(y0) * stride + x0];
    };

    const quint64 total = integral[size_t(height) * stride + width];
    const double globalMean = double(total) / (double(width) * double(height));
    const bool darkText = globalMean >= 128.0; // nền sáng → chữ tối

    std::vector<quint8> mask(size_t(width) * size_t(height), 0);
    for (int y = 0; y < height; ++y) {
        const uchar* line = gray.constScanLine(y);
        const int y0 = std::max(0, y - window);
        const int y1 = std::min(height, y + window + 1);
        for (int x = 0; x < width; ++x) {
            const int x0 = std::max(0, x - window);
            const int x1 = std::min(width, x + window + 1);
            const int count = (x1 - x0) * (y1 - y0);
            const int mean = int(rectSum(x0, y0, x1, y1) / quint64(count));
            const int value = int(line[x]);
            const bool ink = darkText ? (value < mean - threshold)
                                      : (value > mean + threshold);
            if (ink)
                mask[size_t(y) * width + x] = 1;
        }
    }
    return mask;
}

/** Connected-components 8 hướng (flood fill bằng stack tường minh, không đệ quy). */
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

QList<TextBox> HeuristicTextDetector::detect(const QImage& image) const
{
    QList<TextBox> result;
    if (image.isNull())
        return result;

    const WorkingImage work = makeWorkingImage(image, m_options.maxDimension);
    const int width  = work.image.width();
    const int height = work.image.height();
    if (width < 4 || height < 4)
        return result;

    const std::vector<quint64> integral = buildIntegral(work.image);
    const std::vector<quint8> mask = buildInkMask(work.image, integral,
                                                  std::max(1, m_options.window),
                                                  m_options.threshold);
    const std::vector<Component> components = labelComponents(mask, width, height);

    const double inverseScale = (work.scale > 0.0) ? 1.0 / work.scale : 1.0;
    const int maxHeight = std::max(m_options.minHeight + 1, height / 3);

    result.reserve(int(components.size()));
    for (const Component& component : components) {
        const int boxWidth  = component.maxX - component.minX + 1;
        const int boxHeight = component.maxY - component.minY + 1;

        if (boxHeight < m_options.minHeight || boxHeight > maxHeight)
            continue;
        if (boxWidth < 2 || component.area < m_options.minArea)
            continue;

        const float fill = float(component.area) / float(boxWidth * boxHeight);
        if (fill < m_options.minFill || fill > m_options.maxFill)
            continue;

        const QRect rect(int(std::lround(component.minX * inverseScale)),
                         int(std::lround(component.minY * inverseScale)),
                         int(std::lround(boxWidth * inverseScale)),
                         int(std::lround(boxHeight * inverseScale)));

        result.append(TextBox{rect, fill});
    }

    return result;
}

} // namespace EZTranslator
