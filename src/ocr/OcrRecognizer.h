#pragma once

#include "ocr/OcrTypes.h"

#include <QImage>
#include <QList>
#include <QString>

#include <memory>

namespace EZTranslator {

class OcrRuntime;

/**
 * @brief Giai đoạn Recognition: đọc NỘI DUNG của từng box đã có (từ OcrDetector).
 *
 * Model: PP-OCR CTC (en_PP-OCRv4_rec_mobile / ch_PP-OCRv4_rec_infer).
 *  - Crop ảnh theo từng box (không phải cả trang), resize cao 48 giữ tỉ lệ.
 *  - Dòng dài bị chia đoạn ≤ `recMaxWidth` rồi ghép lại (bỏ phần chồng lấn).
 *  - Batch theo bucket bề rộng (gộp các dòng cùng cỡ) → 1 Run cho nhiều dòng.
 *  - Output logits CTC → greedy decode bằng từ điển (bỏ blank và ký tự lặp).
 *
 * Cache theo nội dung dòng: dòng không đổi (cùng chữ, cùng font) được phục vụ
 * từ cache, không chạy model lại.
 */
class OcrRecognizer
{
public:
    OcrRecognizer();
    ~OcrRecognizer();

    OcrRecognizer(const OcrRecognizer&) = delete;
    OcrRecognizer& operator=(const OcrRecognizer&) = delete;

    /** Nạp model + từ điển. Trả false + @p error nếu thất bại. */
    bool load(OcrRuntime& runtime, const OcrOptions& options, QString* error = nullptr);

    [[nodiscard]] bool isLoaded() const;

    /**
     * @brief Đọc chữ cho từng box.
     * @param image Ảnh gốc (box.rect theo hệ toạ độ ảnh này).
     * @return Các box có text (bỏ box đọc rỗng).
     */
    [[nodiscard]] QList<OcrTextBox> recognize(const QImage& image, const QList<OcrTextBox>& boxes);

    [[nodiscard]] OcrTimings lastTimings() const;

    [[nodiscard]] int  lineCacheSize() const;
    void clearLineCache();
    [[nodiscard]] bool saveLineCache(const QString& path) const;
    [[nodiscard]] bool loadLineCache(const QString& path);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace EZTranslator
