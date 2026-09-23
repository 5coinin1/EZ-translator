#pragma once

#include "ocr/OcrTypes.h"

#include <QImage>
#include <QList>

#include <memory>

namespace EZTranslator {

class OcrRuntime;

/**
 * @brief Giai đoạn Detection: tìm VỊ TRÍ các hộp chữ (chưa đọc nội dung).
 *
 * Model: PP-OCR DB (ch_PP-OCRv4_det_infer.onnx, fp32).
 *  - Ảnh ROI được resize sao cho cạnh dài ≤ `detLimitSide` (bội số 32).
 *  - Output heatmap xác suất "là chữ" → threshold → nhị phân hoá.
 *  - Connected components → mỗi blob là một hộp → unclip (`unclipRatio`) nở ra
 *    vì text thật to hơn vùng mực → map ngược về ảnh gốc.
 *
 * Detector luôn fp32: bản fp16 làm box lệch giữa các phiên, phá cache dòng của
 * recognizer.
 */
class OcrDetector
{
public:
    OcrDetector();
    ~OcrDetector();

    OcrDetector(const OcrDetector&) = delete;
    OcrDetector& operator=(const OcrDetector&) = delete;

    /** Nạp model. Trả false + @p error nếu thất bại. */
    bool load(OcrRuntime& runtime, const OcrOptions& options, QString* error = nullptr);

    [[nodiscard]] bool isLoaded() const;

    /** Phát hiện box trong ảnh (toạ độ pixel của chính ảnh đó). */
    [[nodiscard]] QList<OcrTextBox> detect(const QImage& image);

    /** Thời gian (ms) của lần detect() gần nhất. */
    [[nodiscard]] double lastMs() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace EZTranslator
