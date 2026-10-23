#pragma once

#include <atomic>
#include <mutex>
#include <QHash>

#include "ocr/OcrEngine.h"
#include "ocr/OcrTypes.h"
#include "scroll/ScrollOcrReuse.h"

#include <QImage>
#include <QList>
#include <QObject>
#include <QPoint>
#include <QRect>
#include <QString>

namespace EZTranslator {

/**
 * @brief Chạy OCR trên MỘT worker thread (không chặn UI).
 *
 * Mọi yêu cầu OCR (nạp model, OCR vùng, dò dòng khi hover) đi qua đây bằng
 * QueuedConnection; kết quả phát về main thread qua signal. Vì chỉ một thread
 * dùng ONNX session nên tránh được vấn đề DirectML không cho Run song song.
 */
class OcrWorker : public QObject
{
    Q_OBJECT
public:
    explicit OcrWorker(QObject* parent = nullptr);
    ~OcrWorker() override;

public slots:
    /** Nạp model + làm nóng (warm-up) rồi báo `loaded`. */
    void load(const EZTranslator::OcrOptions& options);

    /** OCR một vùng (ảnh ROI) → phát kết quả kèm token/hash của đúng request. */
    void recognizeRegion(const QString& id, quint64 requestToken, quint64 imageHash,
                         qint64 queuedAtMs, const QImage& image, bool scrollReuse);

    /** Luồng UI gọi NGAY TRƯỚC khi xếp `recognizeRegion` vào hàng đợi: worker dùng bộ đếm này để
     *  biết còn yêu cầu nào đang chờ không — lúc cuộn thì BỎ dựng plate (bản cho khung cũ vô ích
     *  và làm kết quả OCR mới phải chờ sau nó). */
    void noteRequestQueued() { ++m_pendingRequests; }
    void noteRegionRequest(const QString& id, quint64 token);

    /**
     * @brief OCR ĐỒNG BỘ, trả kết quả trực tiếp — cho đường OCR TĂNG DẦN khi cuộn.
     *
     * Luồng UI gọi bằng BlockingQueuedConnection để OCR ngay dải mới lộ ra (~1 dòng, ~20-50ms)
     * thay vì chờ OCR lại toàn vùng. Chỉ gọi khi worker đang rảnh (lúc cuộn OCR full bị hoãn).
     */
    QList<EZTranslator::OcrTextBox> recognizeSync(const QImage& image)
    {
        return m_engine.recognize(image);
    }

    /** Dò dòng dưới con trỏ trong dải `band` → phát `hoverProbed`. */
    void probeHover(const QString& id, const QImage& band, const QPoint& cursorLocal,
                    qint64 queuedAtMs);

signals:
    void loaded(bool ok, const QString& provider, const QString& error);
    void regionRecognized(const QString& id, quint64 requestToken, quint64 imageHash,
                          const QList<EZTranslator::OcrTextBox>& boxes);
    void regionScrollMatched(const QString& id, quint64 requestToken, quint64 imageHash,
                             int dy, double score);
    /** Plate (nền đã xoá chữ) của ĐÚNG ảnh vừa OCR — dựng SẴN trên luồng OCR để luồng UI không
     *  phải dựng (100-270ms, từng nằm trên luồng UI và chặn cả dây chuyền OCR/dịch phía sau).
     *  `imageHash` = dHash của ảnh — luồng UI tra plate theo hash nội dung. */
    void regionPlateReady(const QString& id, quint64 requestToken, quint64 imageHash,
                          const QImage& plate);
    void regionFinished(const QString& id, quint64 requestToken);
    /** @param clippedByBand dòng chạm mép dải ⇒ gần như chắc chắn bị cắt đầu/cuối. */
    void hoverProbed(const QString& id, const QString& text, const QRect& lineRect,
                     bool clippedByBand);

private:
    void warmUp();

    EZTranslator::OcrEngine m_engine;
    int m_hoverLineHeight{0};
    /// Số yêu cầu OCR VÙNG đã xếp hàng nhưng chưa chạy (luồng UI tăng, worker giảm). Dùng để bỏ
    /// dựng plate khi còn yêu cầu mới đang chờ (xem recognizeRegion).
    std::atomic<int> m_pendingRequests{0};
    std::mutex m_regionTokensMutex;
    QHash<QString, quint64> m_regionTokens;
    QHash<QString, ScrollOcrReuse> m_scrollReuse;
};

} // namespace EZTranslator
