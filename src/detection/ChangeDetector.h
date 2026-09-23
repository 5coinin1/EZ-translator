#pragma once

#include <QHash>
#include <QImage>
#include <QString>
#include <QtTypes>

namespace EZTranslator {

/**
 * @brief Phát hiện vùng/khung hình có thay đổi để bỏ qua frame tĩnh.
 *
 * Dùng difference hash (dHash 64-bit): ảnh thu nhỏ về 9x8 grayscale, mỗi cặp
 * pixel ngang so sánh để tạo 1 bit → 64 bit. So sánh hash hiện tại với hash
 * lần trước của CÙNG một vùng bằng khoảng cách Hamming.
 *
 * Key theo regionId nên mỗi vùng tự đánh giá độc lập: menu đứng yên trong khi
 * khung chat vẫn đổi sẽ không khiến menu bị OCR/dịch lại (nguyên tắc #2).
 *
 * Không thread-safe: chỉ dùng từ một thread (hiện tại là main thread qua slot).
 */
class ChangeDetector
{
public:
    struct Options
    {
        int maxDistance{4}; ///< Khoảng cách Hamming tối đa vẫn coi là "không đổi"
    };

    explicit ChangeDetector(Options options = {}) : m_options(options) {}

    /**
     * @brief So sánh nội dung vùng @p regionId với lần gọi trước.
     * @return true nếu vùng thay đổi (lần gọi đầu tiên luôn trả true).
     */
    [[nodiscard]] bool hasChanged(const QString& regionId, const QImage& image);

    /** Xóa toàn bộ trạng thái (dùng khi đổi cửa sổ / reset session). */
    void reset() noexcept { m_lastHash.clear(); }

    /** Quên trạng thái của một vùng (dùng khi xóa vùng dịch). */
    void forget(const QString& regionId) { m_lastHash.remove(regionId); }

    /** dHash 64-bit của ảnh; trả 0 nếu ảnh rỗng. */
    [[nodiscard]] static quint64 differenceHash(const QImage& image);

    /** Khoảng cách Hamming giữa hai hash 64-bit (số bit khác nhau). */
    [[nodiscard]] static int hammingDistance(quint64 a, quint64 b) noexcept;

private:
    QHash<QString, quint64> m_lastHash;
    Options m_options;
};

} // namespace EZTranslator
