#pragma once

#include <QRect>
#include <QString>
#include <QtTypes>

#include <chrono>

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
    bool binarize{false}; ///< thử nghiệm: nhị phân hoá (Otsu) ảnh vùng trước khi detect/rec
    int  threads{0};      ///< intra-op cho detector (0 = ORT tự chọn)
    int  recThreads{1};   ///< intra-op cho recognition (song song nằm ở batch)

    // ── Detection ────────────────────────────────────────────────────────────
    float detThreshold{0.3f};  ///< ngưỡng xác suất "là chữ"
    float unclipRatio{1.6f};   ///< nở box (text thật to hơn vùng mực)
    int   detLimitSide{640};   ///< cạnh dài ảnh đưa vào det được resize ≤ giá trị này

    // ── Recognition ──────────────────────────────────────────────────────────
    bool disableRecBatch{true};  ///< tắt batch (mặc định; CPU không lợi, GPU tự bật)
    int  lineCacheLimit{4096};   ///< cache rec theo nội dung dòng (0 = tắt)
    int  recMaxWidth{1920};      ///< dòng dài hơn sẽ bị chia đoạn ≤ giá trị này
};

/** Thời gian/chi phí của lần recognize() gần nhất. */
struct OcrTimings
{
    double totalMs{0.0};  ///< toàn bộ OcrEngine::recognize(), gồm preprocess/merge/tile
    double detMs{0.0};
    double recMs{0.0};
    int    boxes{0};
    int    detCalls{0};   ///< số lần detector forward trong request
    int    tileCount{0};  ///< số tile/đường detect đã chạy
    int    altCalls{0};   ///< số lần gọi recognizer model dự phòng
    int    recCalls{0};    ///< số forward pass (một batch gộp nhiều dòng)
    int    recCached{0};   ///< số dòng lấy từ cache (không chạy model)
    bool   recBatched{false};
    double recRunMs{0.0};  ///< phần Run (GPU compute + upload/download)
    double recDecodeMs{0.0}; ///< phần CTC decode trên CPU
    qint64 recOutputBytes{0};
};

/** Đồng hồ monotonic dùng chung giữa UI thread và OCR worker để đo queue wait. */
inline qint64 ocrMonotonicMs()
{
    using Clock = std::chrono::steady_clock;
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               Clock::now().time_since_epoch())
        .count();
}

} // namespace EZTranslator
