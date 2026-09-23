#pragma once

#include <QImage>
#include <QList>
#include <QString>

#include "detection/TextBox.h"

namespace EZTranslator {

/**
 * @brief Interface thuần ảo cho bộ phát hiện vùng chữ.
 *
 * Detection KHÔNG đọc nội dung chữ (đó là việc của OCR) — nó chỉ trả về
 * VỊ TRÍ các vùng có khả năng chứa chữ trong ảnh đầu vào.
 *
 * Implementation phải stateless theo nghĩa: cùng ảnh đầu vào cho cùng kết quả.
 * Nhờ vậy detect() được khai báo const và an toàn khi gọi từ worker thread.
 */
class ITextDetector
{
public:
    virtual ~ITextDetector() = default;

    /**
     * @brief Phát hiện các vùng có chữ trong @p image.
     * @param image Ảnh RGB/Grayscale (thường là ROI đã crop).
     * @return Danh sách TextBox theo pixel; rỗng nếu không tìm thấy hoặc ảnh null.
     */
    [[nodiscard]] virtual QList<TextBox> detect(const QImage& image) const = 0;

    /** Tên định danh của detector (dùng cho log/cấu hình). */
    [[nodiscard]] virtual QString name() const = 0;
};

} // namespace EZTranslator
