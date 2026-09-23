#pragma once

#include <QRect>
#include <QString>
#include <QtTypes>

namespace EZTranslator {

/**
 * @brief Một hộp chữ trong pipeline OCR.
 *
 * Detection chỉ điền `rect` (chưa biết chữ gì); Recognition điền `text` +
 * `confidence`. Tách hai giai đoạn nên box có thể tồn tại mà chưa có text.
 */
struct OcrTextBox
{
    QRect   rect;
    QString text;
    float   confidence{0.0f};
};

/**
 * @brief Cấu hình pipeline OCR (detection + recognition).
 */
struct OcrOptions
{
    // Đường dẫn model / từ điển
    QString detModelPath;
    QString recModelPath;
    QString dictionaryPath;

    bool useGpu{true};
    int  threads{0};      ///< intra-op cho detector (0 = ORT tự chọn)
    int  recThreads{1};   ///< intra-op cho recognition (song song nằm ở batch)

    // ── Detection ────────────────────────────────────────────────────────────
    float detThreshold{0.3f};  ///< ngưỡng xác suất "là chữ"
    float unclipRatio{1.6f};   ///< nở box (text thật to hơn vùng mực)
    int   detLimitSide{640};   ///< cạnh dài ảnh đưa vào det được resize ≤ giá trị này

    // ── Recognition ──────────────────────────────────────────────────────────
    bool disableRecBatch{false}; ///< tắt batch (mặc định bật trên GPU)
    int  lineCacheLimit{4096};   ///< cache rec theo nội dung dòng (0 = tắt)
    int  recMaxWidth{1920};      ///< dòng dài hơn sẽ bị chia đoạn ≤ giá trị này
};

/** Thời gian/chi phí của lần recognize() gần nhất. */
struct OcrTimings
{
    double detMs{0.0};
    double recMs{0.0};
    int    boxes{0};
    int    recCalls{0};    ///< số forward pass (một batch gộp nhiều dòng)
    int    recCached{0};   ///< số dòng lấy từ cache (không chạy model)
    bool   recBatched{false};
    double recRunMs{0.0};  ///< phần Run (GPU compute + upload/download)
    double recDecodeMs{0.0}; ///< phần CTC decode trên CPU
    qint64 recOutputBytes{0};
};

} // namespace EZTranslator
