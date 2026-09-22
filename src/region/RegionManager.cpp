#include "region/RegionManager.h"

#include <QDebug>
#include <algorithm>

namespace EZTranslator {

// ── Constants ────────────────────────────────────────────────────────────────

// Dung sai float cho phép (drag/resize UI có thể tạo sai số nhỏ)
static constexpr double kTolerance = 0.01;
// Ngưỡng reject giá trị bất hợp lý (tránh accept x = -10, width = 50)
static constexpr double kRejectThreshold = 0.5;

// ── Region management ─────────────────────────────────────────────────────────

void RegionManager::setRegions(const QList<TranslationRegion>& regions)
{
    m_regions = regions;
}

void RegionManager::addRegion(const TranslationRegion& region)
{
    m_regions.append(region);
}

bool RegionManager::updateRegion(const TranslationRegion& region)
{
    for (auto& r : m_regions) {
        if (r.id == region.id) {
            r = region;
            return true;
        }
    }
    return false;
}

bool RegionManager::removeRegion(const QString& regionId)
{
    const int before = m_regions.size();
    m_regions.removeIf([&regionId](const TranslationRegion& r) {
        return r.id == regionId;
    });
    return m_regions.size() < before;
}

void RegionManager::clear()
{
    m_regions.clear();
}

// ── Coordinate utilities ──────────────────────────────────────────────────────

bool RegionManager::isNormalizedRectValid(const NormalizedRect& norm) noexcept
{
    // Reject bất hợp lý nghiêm trọng
    if (norm.x      < -kRejectThreshold) return false;
    if (norm.y      < -kRejectThreshold) return false;
    if (norm.width  <= 0.0             ) return false;
    if (norm.height <= 0.0             ) return false;
    if (norm.width  > 1.0 + kRejectThreshold) return false;
    if (norm.height > 1.0 + kRejectThreshold) return false;
    if ((norm.x + norm.width)  > 1.0 + kRejectThreshold) return false;
    if ((norm.y + norm.height) > 1.0 + kRejectThreshold) return false;

    // Sau khi qua strict check, cho phép sai số nhỏ (sẽ clamp khi convert)
    return true;
}

QRect RegionManager::normalizedToPixelRect(
    const NormalizedRect& norm,
    int frameWidth,
    int frameHeight) noexcept
{
    if (frameWidth <= 0 || frameHeight <= 0) return {};
    if (!isNormalizedRectValid(norm))        return {};

    // Clamp về [0, 1] sau khi qua strict validation
    const double x = std::clamp(norm.x,     0.0, 1.0);
    const double y = std::clamp(norm.y,     0.0, 1.0);
    const double w = std::clamp(norm.width,  0.0, 1.0 - x);
    const double h = std::clamp(norm.height, 0.0, 1.0 - y);

    // Convert sang pixel (làm tròn an toàn)
    const int px = static_cast<int>(std::round(x * frameWidth));
    const int py = static_cast<int>(std::round(y * frameHeight));
    const int pw = static_cast<int>(std::round(w * frameWidth));
    const int ph = static_cast<int>(std::round(h * frameHeight));

    if (pw <= 0 || ph <= 0) return {};

    // Intersect với frame bounds để đảm bảo không vượt biên
    const QRect frameBounds(0, 0, frameWidth, frameHeight);
    const QRect candidate(px, py, pw, ph);
    const QRect result = candidate.intersected(frameBounds);

    if (result.isEmpty()) return {};
    return result;
}

// ── Core pipeline ─────────────────────────────────────────────────────────────

QList<RegionFrame> RegionManager::extractRegions(const CapturedFrame& frame) const
{
    QList<RegionFrame> result;

    // Guard: frame rỗng hoặc không hợp lệ
    if (frame.image.isNull()
        || frame.image.width()  <= 0
        || frame.image.height() <= 0) {
        qWarning() << "[RegionManager] Received null/empty CapturedFrame, skip.";
        return result;
    }

    const int fw = frame.image.width();
    const int fh = frame.image.height();

    for (const TranslationRegion& region : m_regions) {
        // Skip region bị tắt
        if (!region.enabled) continue;

        // Convert + validate tọa độ
        const QRect pixelRect = normalizedToPixelRect(region.normalizedRect, fw, fh);
        if (pixelRect.isEmpty()) {
            qDebug() << "[RegionManager] Region" << region.id
                     << "(" << region.name << ") produces empty pixelRect, skip."
                     << "normalizedRect =("
                     << region.normalizedRect.x << region.normalizedRect.y
                     << region.normalizedRect.width << region.normalizedRect.height << ")";
            continue;
        }

        // Crop ảnh tại resolution gốc (QImage::copy() dùng implicit sharing – không copy pixel nếu không cần)
        const QImage cropped = frame.image.copy(pixelRect);
        if (cropped.isNull()) {
            qWarning() << "[RegionManager] QImage::copy() returned null for region" << region.id;
            continue;
        }

        RegionFrame rf;
        rf.regionId       = region.id;
        rf.regionName     = region.name;
        rf.regionOrder    = region.orderNumber;
        rf.pixelRect      = pixelRect;
        rf.sourceFrameSize = QSize(fw, fh);
        rf.image          = cropped;
        rf.timestamp      = frame.timestamp;

        result.append(std::move(rf));
    }

    return result;
}

} // namespace EZTranslator
