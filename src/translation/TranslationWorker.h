#pragma once

#include "translation/DocumentTranslator.h"
#include "translation/ITranslator.h"
#include "translation/TranslationCache.h"
#include "translation/TranslationConfig.h"

#include <QObject>
#include <QHash>
#include <QQueue>
#include <QString>

#include <atomic>
#include <memory>
#include <optional>

namespace EZTranslator {

/**
 * @brief Chạy dịch trên worker thread (không chặn UI).
 *
 * Nhận yêu cầu qua slot `translate(id, text, token)` (gọi bằng QueuedConnection),
 * phát `translated(id, token, doc)` khi xong. `token` tăng dần để tầng trên bỏ qua
 * kết quả cũ (đã có yêu cầu mới hơn cho cùng vùng).
 *
 * Engine (offline / Google / AI) và cache đều sống trong worker thread. Cấu hình
 * được truyền qua `configure(TranslationConfig)` mỗi khi người dùng lưu Settings.
 */
class TranslationWorker : public QObject
{
    Q_OBJECT
public:
    explicit TranslationWorker(QObject* parent = nullptr);
    ~TranslationWorker() override;

    /**
     * @brief Báo worker biết đang có yêu cầu ưu tiên cao (hover) chờ trong hàng đợi.
     *
     * Gọi từ thread chủ NGAY khi gửi yêu cầu hover. Lượt dịch vùng đang chạy sẽ
     * nhường event loop giữa các khối nên hover không phải chờ hết cả trang.
     */
    void markInteractivePending() { m_interactivePending.store(true); }

public slots:
    /** Đặt cấu hình engine dịch (ngôn ngữ, engine, API key, system prompt). */
    void configure(const EZTranslator::TranslationConfig& config);

    /** Người dùng yêu cầu dịch lại: bỏ bản dịch lưu trong phiên worker. */
    void clearCache();

    /** Xếp một văn bản; mailbox tự gộp các lượt vùng trùng id. */
    void translate(const QString& id, const QString& text, quint64 token,
                   qint64 queuedAtMs = 0);

    /** Xếp hover ưu tiên cao — có thể chen vào giữa lượt dịch vùng. */
    void translateInteractive(const QString& id, const QString& text, quint64 token,
                              qint64 queuedAtMs = 0);

signals:
    void translated(const QString& id, quint64 token,
                    const EZTranslator::TranslatedDocument& document);

private:
    struct PendingRequest
    {
        QString id;
        QString text;
        quint64 token{0};
        qint64 queuedAtMs{0};
        bool interactive{false};
    };

    void enqueueRequest(PendingRequest request);
    void drainRequests();
    void handleTranslate(const QString& id, const QString& text, quint64 token, bool interactive);
    QStringList runEngines(const QStringList& units);

    TranslationCache m_cache;
    std::unique_ptr<ITranslator> m_ct2;      ///< ưu tiên 1: CTranslate2 int8 (nhanh nhất)
    QString m_ct2ModelDir;                   ///< model dir sidecar đang phục vụ (đổi cặp = đổi model)
    QString m_ct2Device;                     ///< auto/cpu/cuda đang dùng
    std::unique_ptr<ITranslator> m_marian;   ///< ưu tiên 2: Marian ONNX fp32
    std::unique_ptr<ITranslator> m_primary;  ///< online: Google (API key hoặc gtx)
    std::unique_ptr<ITranslator> m_fallback; ///< online dự phòng: MyMemory
    std::unique_ptr<ITranslator> m_ai;       ///< AI/LLM (OpenAI-compatible)
    QList<ITranslator*> m_offlineChain;      ///< các engine offline đang dùng được
    std::atomic<bool> m_interactivePending{false};
    QHash<QString, PendingRequest> m_pendingRegions;
    QQueue<QString> m_regionOrder;
    std::optional<PendingRequest> m_pendingInteractive;
    bool m_processing{false};
    bool m_processingInteractive{false};
    EZTranslator::TranslationConfig m_config;
};

} // namespace EZTranslator
