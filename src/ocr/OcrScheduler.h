#pragma once

#include <QHash>
#include <QImage>
#include <QString>
#include <QtTypes>

namespace EZTranslator {

/** Quyết định có chạy OCR cho một vùng trong tick này hay không. */
struct OcrScheduleDecision
{
    bool run{false};      ///< chạy OCR ngay tick này
    bool changed{false};  ///< nội dung vùng khác lần nhận gần nhất
    bool deferred{false}; ///< đang đổi / chưa settle / bị cap → chưa chạy
};

/**
 * @brief Lên lịch chạy OCR theo từng vùng, thay cho việc "đổi là chạy ngay".
 *
 * Ba hiệu ứng (học từ pipeline live của bản tham khảo):
 *  1. **settle**: vùng phải đứng yên `settleMs` ms mới chạy OCR — tránh OCR giữa
 *     lúc nội dung/hoạt ảnh còn đang đổi.
 *  2. **cooldown**: hai lần chạy OCR cùng vùng cách nhau tối thiểu `minRunIntervalMs`.
 *  3. **cap**: tối đa `maxRunsPerTick` lần chạy mỗi tick (khi có nhiều vùng).
 *
 * Không thread-safe: dùng từ một thread (main thread qua slot, như hiện tại).
 */
class OcrScheduler
{
public:
    struct Options
    {
        int    maxDistance{4};        ///< khoảng cách Hamming tối đa vẫn coi là "không đổi"
        qint64 settleMs{15};           ///< vùng đứng yên bao lâu mới chạy OCR (hạ 130→60→30→15; cửa
                                      ///< sổ chuyển động 80ms mới là thứ chặn OCR giữa lúc cuộn)
        qint64 minRunIntervalMs{60};  ///< khoảng cách tối thiểu giữa 2 lần chạy OCR (hạ từ 600)
        int    maxRunsPerTick{1};     ///< cap số lần chạy mỗi tick
    };

    OcrScheduler() = default;
    explicit OcrScheduler(Options options) : m_options(options) {}

    /** Bắt đầu một tick mới (reset bộ đếm cap). */
    void beginTick() noexcept { m_runsThisTick = 0; }

    /** Ngưỡng Hamming vẫn coi là "cùng nội dung" — cache box dùng chung ngưỡng này để nhiễu
     *  capture 1-4 bit không làm cache trượt mỗi khung (log: OCR full ~17 lần/giây đứng yên). */
    [[nodiscard]] int maxDistance() const noexcept { return m_options.maxDistance; }

    /** Gọi cho từng vùng một lần mỗi tick. */
    [[nodiscard]] OcrScheduleDecision process(const QString& regionId, const QImage& image,
                                              qint64 nowMs);
    [[nodiscard]] OcrScheduleDecision process(const QString& regionId, const QImage& image,
                                              qint64 nowMs, quint64 contentHash);

    /**
     * Đánh dấu vùng vừa được phục vụ từ CACHE BOX (nội dung đã từng OCR, không chạy lại OCR).
     * Cập nhật cooldown để các tick sau không thúc OCR lại cùng nội dung đó.
     */
    void noteServedFromCache(const QString& regionId, qint64 nowMs)
    {
        Region& region = m_regions[regionId];
        region.lastRunMs = nowMs;
    }

    /** Quên trạng thái một vùng (khi xóa vùng dịch). */
    void forget(const QString& regionId) { m_regions.remove(regionId); }

    /** Xóa toàn bộ trạng thái (đổi cửa sổ / reset session). */
    void reset() noexcept
    {
        m_regions.clear();
        m_runsThisTick = 0;
    }

private:
    struct Region
    {
        quint64 hash{0};
        qint64  changedAt{0};   ///< thời điểm nội dung đổi lần cuối
        qint64  lastRunMs{-1};  ///< lần chạy OCR gần nhất (-1 = chưa từng chạy)
        bool    known{false};
    };

    QHash<QString, Region> m_regions;
    Options m_options;
    int m_runsThisTick{0};
};

} // namespace EZTranslator
