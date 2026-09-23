#pragma once

#include "detection/ITextDetector.h"

namespace EZTranslator {

/**
 * @brief Phát hiện vùng chữ bằng heuristic — không cần model AI.
 *
 * Quy trình:
 *  1. Chuyển grayscale, downscale nếu ảnh quá lớn (giữ tỉ lệ) để đảm bảo tốc độ.
 *  2. Dựng integral image → lấy mean cục bộ O(1) mỗi pixel.
 *  3. Ngưỡng thích nghi: pixel là "mực" nếu lệch khỏi mean cục bộ quá threshold.
 *     Tự nhận biết chữ sáng trên nền tối hoặc ngược lại nhờ so mean toàn cục.
 *  4. Connected-components 8 hướng gom các điểm mực thành ký tự/khối.
 *  5. Lọc theo chiều cao, diện tích, tỉ lệ lấp đầy → trả TextBox.
 *
 * Phù hợp cho text UI game/truyện có độ tương phản rõ. Với nền phức tạp nên
 * thay bằng detector dựa trên model ở phase sau.
 */
class HeuristicTextDetector final : public ITextDetector
{
public:
    struct Options
    {
        int   maxDimension{1600}; ///< Cạnh dài nhất sau downscale (giữ tỉ lệ)
        int   window{12};         ///< Bán kính cửa sổ lấy mean cục bộ
        int   threshold{10};      ///< Độ lệch so với mean để tính là mực (0..255)
        int   minHeight{6};       ///< Chiều cao tối thiểu của một kết nối
        int   minArea{4};         ///< Diện tích tối thiểu
        float minFill{0.08f};     ///< Tỉ lệ lấp đầy tối thiểu
        float maxFill{0.95f};     ///< Tỉ lệ lấp đầy tối đa
    };

    HeuristicTextDetector() = default;
    explicit HeuristicTextDetector(Options options) : m_options(options) {}

    [[nodiscard]] QList<TextBox> detect(const QImage& image) const override;
    [[nodiscard]] QString name() const override { return QStringLiteral("heuristic"); }

private:
    Options m_options;
};

} // namespace EZTranslator
