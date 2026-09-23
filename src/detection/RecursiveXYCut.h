#pragma once

#include <QList>
#include <QRect>

namespace EZTranslator {

/** Tùy chọn cho thuật toán cắt đệ quy XY. */
struct XYCutOptions
{
    int   minGapPixels{8};       ///< Khe hở tối thiểu (pixel) để coi là ranh giới
    float columnGapFactor{1.0f}; ///< Ngưỡng khe dọc = chiều cao trội * factor
    float rowGapFactor{1.2f};    ///< Ngưỡng khe ngang = chiều cao trội * factor
    int   maxDepth{32};          ///< Giới hạn độ sâu đệ quy
};

/**
 * @brief Gom các TextBox rời rạc thành các block văn bản bằng Recursive XY-Cut.
 *
 * Ý tưởng: tìm "thung lũng" (khe hở lớn nhất) theo trục X và Y; nếu khe lớn hơn
 * ngưỡng thì cắt tại đó rồi đệ quy hai nửa, ngược lại trả về hình bao của cụm.
 * Kết quả là các hình chữ nhật bao trọn từng khối/cột chữ.
 *
 * Dùng để chuyển kết quả ITextDetector (nhiều box nhỏ) thành các vùng logic
 * cho OCR/overlay.
 */
class RecursiveXYCut
{
public:
    [[nodiscard]] static QList<QRect> cut(const QList<QRect>& rects,
                                          const XYCutOptions& options = {});
};

} // namespace EZTranslator
