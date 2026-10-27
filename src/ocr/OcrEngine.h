#pragma once

#include "ocr/OcrTypes.h"

#include <QImage>
#include <QList>
#include <QString>

#include <memory>
#include <functional>

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
    /**
     * @brief Callback nhận các box đã nhận dạng xong trong lúc full OCR còn chạy.
     *
     * Chỉ dùng cho CPU: recognition được chia thành nhiều cụm để lớp ứng dụng có thể
     * dịch/hiện cụm đầu tiên sớm, thay vì chờ toàn bộ ảnh. Callback chạy trên worker
     * thread và nhận danh sách LŨY KẾ các box đã xong (thứ tự detection).
     */
    using ProgressFn = std::function<void(const QList<OcrTextBox>& boxes)>;

    OcrEngine();
    ~OcrEngine();

    OcrEngine(const OcrEngine&) = delete;
    OcrEngine& operator=(const OcrEngine&) = delete;

    /** Nạp model det + rec + từ điển. Trả false + @p error nếu thất bại. */
    bool load(const OcrOptions& options, QString* error = nullptr);

    [[nodiscard]] bool isLoaded() const;

    /** Provider thực tế dạng "Det:.../Rec:...". */
    [[nodiscard]] QString providerName() const;
    [[nodiscard]] QString detProviderName() const;
    [[nodiscard]] QString recProviderName() const;

    /** Detection rồi Recognition; trả box có text (chưa assemble). */
    [[nodiscard]] QList<OcrTextBox> recognize(const QImage& image,
                                              const std::function<bool()>& cancelled = {},
                                              const ProgressFn& progress = {});

    [[nodiscard]] OcrTimings lastTimings() const;

    [[nodiscard]] OcrDetector& detector();
    [[nodiscard]] OcrRecognizer& recognizer();

private:
    /** Nạp model chữ của HỆ CHỮ CÒN LẠI (en ↔ zh) theo nhu cầu — xem recognize(). */
    QList<OcrTextBox> recognizeProcessed(const QImage& image, bool allowAlt = true,
                                         const ProgressFn& progress = {});
    /** Nhận dạng các box đã detect, dùng chung cho path thường và tile detect-once. */
    QList<OcrTextBox> recognizeBoxes(const QImage& image, const QList<OcrTextBox>& boxes,
                                     bool allowAlt);

    bool ensureAltRecognizer();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
    std::function<bool()> m_cancelled;
};

} // namespace EZTranslator
