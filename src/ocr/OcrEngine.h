#pragma once

#include "ocr/OcrTypes.h"

#include <QImage>
#include <QList>
#include <QString>

#include <memory>

namespace EZTranslator {

class OcrDetector;
class OcrRecognizer;
class OcrRuntime;

/**
 * @brief Điều phối pipeline OCR: Detection → Recognition.
 *
 * Giữ hai giai đoạn tách biệt (OcrDetector / OcrRecognizer) nhưng cung cấp một
 * điểm vào tiện dụng cho tầng ứng dụng. Việc ghép văn bản do OcrTextAssembler lo.
 */
class OcrEngine
{
public:
    OcrEngine();
    ~OcrEngine();

    OcrEngine(const OcrEngine&) = delete;
    OcrEngine& operator=(const OcrEngine&) = delete;

    /** Nạp model det + rec + từ điển. Trả false + @p error nếu thất bại. */
    bool load(const OcrOptions& options, QString* error = nullptr);

    [[nodiscard]] bool isLoaded() const;

    /** Provider thực tế của session ("DirectML"/"CUDA"/"CPU"). */
    [[nodiscard]] QString providerName() const;

    /** Detection rồi Recognition; trả box có text (chưa assemble). */
    [[nodiscard]] QList<OcrTextBox> recognize(const QImage& image);

    [[nodiscard]] OcrTimings lastTimings() const;

    [[nodiscard]] OcrDetector& detector();
    [[nodiscard]] OcrRecognizer& recognizer();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace EZTranslator
