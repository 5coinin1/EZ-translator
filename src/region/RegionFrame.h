#pragma once

#include <QImage>
#include <QRect>
#include <QSize>
#include <QString>
#include <QtTypes>

namespace EZTranslator {

/**
 * @brief Kết quả crop một vùng dịch (ROI) từ CapturedFrame.
 *
 * RegionFrame là output của RegionManager::extractRegions().
 * Nó giữ đầy đủ thông tin để:
 *  - Detection: so sánh với RegionFrame trước đó (dùng regionId + timestamp).
 *  - OCR: nhận ảnh crop và xử lý.
 *  - Overlay: dùng pixelRect để biết VỊ TRÍ đặt bản dịch trên target window.
 *
 * QUI TẮC: RegionFrame KHÔNG chứa text nhận dạng hay bản dịch.
 *           Đó là trách nhiệm của OCRResult / TranslationResult.
 */
struct RegionFrame
{
    // ── Identity ────────────────────────────────────────────────────────────
    QString regionId;       ///< ID của TranslationRegion đã sinh ra frame này
    QString regionName;     ///< Tên vùng (dùng để debug/log)
    int     regionOrder{0}; ///< orderNumber của TranslationRegion

    // ── Spatial info (always relative to ORIGINAL CapturedFrame) ────────────
    QRect   pixelRect;         ///< Tọa độ pixel ROI trên frame gốc (KHÔNG scale)
    QSize   sourceFrameSize;   ///< Kích thước full frame đã crop từ đó

    // ── Image data ──────────────────────────────────────────────────────────
    QImage  image;             ///< Ảnh đã crop tại resolution gốc (QImage implicit-sharing)

    // ── Timing ──────────────────────────────────────────────────────────────
    qint64  timestamp{0};      ///< CapturedFrame::timestamp – tính bằng msec từ epoch

    // ── Helpers ─────────────────────────────────────────────────────────────

    /** Trả về true nếu frame hợp lệ (có ảnh và pixelRect > 0) */
    [[nodiscard]] bool isValid() const noexcept
    {
        return !image.isNull() && pixelRect.width() > 0 && pixelRect.height() > 0;
    }
};

} // namespace EZTranslator
