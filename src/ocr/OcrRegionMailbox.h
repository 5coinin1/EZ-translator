#pragma once

#include <QHash>
#include <QImage>
#include <QQueue>
#include <QString>

#include <optional>

namespace EZTranslator {

/**
 * @brief Bounded latest-wins mailbox cho OCR vùng.
 *
 * Mailbox này chỉ được dùng trên luồng điều phối (AppController). Nó giữ tối đa
 * một request đang chạy và một request mới nhất cho mỗi region. Các region chờ
 * được dispatch theo round-robin để một region cập nhật liên tục không chiếm hết
 * lượt của các region còn lại.
 */
class OcrRegionMailbox
{
public:
    struct Request
    {
        QImage image;
        quint64 requestToken{0};
        quint64 imageHash{0};
    };

    struct Dispatch
    {
        QString regionId;
        Request request;
    };

    enum class SubmitResult
    {
        Queued,
        Coalesced,
    };

    SubmitResult submit(const QString& regionId, Request request);

    /** Lấy request tiếp theo; trả rỗng khi đã có request đang chạy. */
    std::optional<Dispatch> startNext();

    /** Kết thúc request đang chạy và cho phép request kế tiếp được lấy. */
    void finish(const QString& regionId);

    /** Bỏ pending của một region, nhưng giữ active để chờ worker báo hoàn tất. */
    void remove(const QString& regionId);

    /** Xoá toàn bộ state khi worker đã được ngắt/dừng. */
    void clear();

    /** Xoá request chờ nhưng giữ active request hiện tại. */
    void clearPending();

    [[nodiscard]] bool isActive(const QString& regionId) const;
    [[nodiscard]] bool hasActive() const { return !m_activeRegion.isEmpty(); }
    [[nodiscard]] int pendingCount() const { return m_pending.size(); }
    [[nodiscard]] int readyCount() const { return m_readyRegions.size(); }

private:
    QHash<QString, Request> m_pending;
    QQueue<QString> m_readyRegions;
    QString m_activeRegion;
};

} // namespace EZTranslator
