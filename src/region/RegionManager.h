#pragma once

#include "region/RegionFrame.h"
#include "capture/IWindowCapture.h"   // CapturedFrame
#include "core/Types.h"               // TranslationRegion, NormalizedRect

#include <QList>

namespace EZTranslator {

/**
 * @brief RegionManager – chuyển đổi CapturedFrame thành RegionFrame[].
 *
 * Trách nhiệm:
 *  1. Lưu danh sách TranslationRegion do user tạo.
 *  2. Validate và reject region không hợp lệ.
 *  3. Convert NormalizedRect → pixel QRect theo kích thước CapturedFrame.
 *  4. Crop QImage từ CapturedFrame.image theo pixelRect đã tính.
 *  5. Trả RegionFrame[] cho pipeline tiếp theo (Detection).
 *
 * KHÔNG phải QObject – RegionManager là pure logic class, không emit signal.
 * Thread-safety: extractRegions() chỉ đọc (const), setRegions() được gọi từ
 * UI thread. Gọi extractRegions() từ capture worker thread ổn vì QImage
 * implicit-sharing safe khi không có writer song song.
 *
 * Giới hạn hợp lệ NormalizedRect (strict validation):
 *   - x, y  ∈ [-0.01, 1.0)    (cho phép sai số float nhỏ)
 *   - width, height ∈ (0, 1.0]
 *   - x + width  ≤ 1.01
 *   - y + height ≤ 1.01
 *  → Clamp về [0,1] sau khi qua strict check.
 *  → Reject nếu vượt quá ngưỡng (x < -0.1, width > 2, …).
 */
class RegionManager
{
public:
    RegionManager() = default;
    ~RegionManager() = default;

    // Non-copyable (danh sách region nên được chia sẻ qua pointer / ref rõ ràng)
    RegionManager(const RegionManager&)            = delete;
    RegionManager& operator=(const RegionManager&) = delete;
    RegionManager(RegionManager&&)                 = default;
    RegionManager& operator=(RegionManager&&)      = default;

    // ── Region management ────────────────────────────────────────────────────

    /** Thay thế toàn bộ danh sách region (từ RegionEditor hoặc profile load). */
    void setRegions(const QList<TranslationRegion>& regions);

    /** Trả về danh sách region hiện tại (read-only). */
    [[nodiscard]] const QList<TranslationRegion>& regions() const noexcept { return m_regions; }

    /** Trả về danh sách region hiện tại (có thể modify). */
    [[nodiscard]] QList<TranslationRegion>& regions() noexcept { return m_regions; }

    /** Thêm một region vào cuối danh sách. */
    void addRegion(const TranslationRegion& region);

    /**
     * Cập nhật region có cùng id.
     * @return true nếu tìm thấy và đã cập nhật.
     */
    bool updateRegion(const TranslationRegion& region);

    /**
     * Xóa region theo id.
     * @return true nếu tìm thấy và đã xóa.
     */
    bool removeRegion(const QString& regionId);

    /** Xóa sạch tất cả region. */
    void clear();

    /** Số lượng region đang quản lý. */
    [[nodiscard]] int count() const noexcept { return m_regions.size(); }

    /** Trả về true nếu không có region nào. */
    [[nodiscard]] bool isEmpty() const noexcept { return m_regions.isEmpty(); }

    // ── Core pipeline ────────────────────────────────────────────────────────

    /**
     * @brief Crop tất cả region đang enabled từ CapturedFrame.
     *
     * Quy trình:
     *  1. Kiểm tra frame hợp lệ (không null, kích thước > 0).
     *  2. Với mỗi region (enabled = true):
     *     a. Validate NormalizedRect.
     *     b. Convert → pixelRect.
     *     c. Intersect với frame.image.rect() (clamp, không crash).
     *     d. Crop QImage.
     *     e. Build RegionFrame.
     *  3. Trả QList<RegionFrame>.
     *
     * Thread-safety: có thể gọi từ capture worker thread nếu setRegions()
     * không được gọi đồng thời. Trong pipeline hiện tại, setRegions() chỉ
     * được gọi từ UI thread khi user Save region, còn extractRegions() được
     * gọi từ onCaptureFrame slot (vẫn chạy trên main thread qua QueuedConnection).
     *
     * @param frame CapturedFrame từ GdiWindowCapture.
     * @return Danh sách RegionFrame đã crop, bỏ qua region không hợp lệ/disabled.
     */
    [[nodiscard]] QList<RegionFrame> extractRegions(const CapturedFrame& frame) const;

    // ── Coordinate utilities (public để unit-test) ───────────────────────────

    /**
     * @brief Convert NormalizedRect → QRect pixel, clamp vào frameBounds.
     *
     * @param norm         Tọa độ chuẩn hóa (có thể có sai số float nhỏ).
     * @param frameWidth   Chiều rộng ảnh gốc (pixels).
     * @param frameHeight  Chiều cao ảnh gốc (pixels).
     * @return QRect hợp lệ đã clamp, hoặc QRect() rỗng nếu không hợp lệ.
     */
    [[nodiscard]] static QRect normalizedToPixelRect(
        const NormalizedRect& norm,
        int frameWidth,
        int frameHeight) noexcept;

    /**
     * @brief Strict validation NormalizedRect.
     *
     * Cho phép sai số nhỏ (±0.01) do floating-point drag/resize.
     * Reject giá trị bất hợp lý như width = 50 hay x = -10.
     *
     * @return true nếu đây là region hợp lệ sau khi xem xét dung sai.
     */
    [[nodiscard]] static bool isNormalizedRectValid(const NormalizedRect& norm) noexcept;

private:
    QList<TranslationRegion> m_regions;
};

} // namespace EZTranslator
