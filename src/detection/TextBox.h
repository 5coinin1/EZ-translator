#pragma once

#include <QRect>

namespace EZTranslator {

/**
 * @brief Hộp bao quanh một vùng có chữ.
 *
 * Tọa độ tính theo pixel của ảnh đầu vào đưa cho ITextDetector
 * (thường là ảnh ROI đã crop từ RegionFrame::image).
 */
struct TextBox
{
    QRect rect;               ///< Hộp bao chữ (pixel, hệ tọa độ ảnh input)
    float confidence{1.0f};   ///< Độ tin cậy 0..1 (heuristic: tỉ lệ lấp đầy)
};

} // namespace EZTranslator
