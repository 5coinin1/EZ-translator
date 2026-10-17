#pragma once

#include "translation/DocumentTranslator.h"
#include "translation/ITranslator.h"
#include "translation/TranslationCache.h"

#include <QObject>
#include <QString>

#include <memory>

namespace EZTranslator {

/**
 * @brief Chạy dịch trên worker thread (không chặn UI).
 *
 * Nhận yêu cầu qua slot `translate(id, text, token)` (gọi bằng QueuedConnection),
 * phát `translated(id, token, doc)` khi xong. `token` tăng dần để tầng trên bỏ qua
 * kết quả cũ (đã có yêu cầu mới hơn cho cùng vùng).
 *
 * Engine online (Google + MyMemory dự phòng) và cache đều sống trong worker thread.
 */
class TranslationWorker : public QObject
{
    Q_OBJECT
public:
    explicit TranslationWorker(QObject* parent = nullptr);
    ~TranslationWorker() override;

public slots:
    /** Đặt cặp ngôn ngữ; tạo engine (trong thread này). */
    void configure(const QString& source, const QString& target);

    /** Dịch một văn bản; phát `translated` với cùng id/token khi xong. */
    void translate(const QString& id, const QString& text, quint64 token);

signals:
    void translated(const QString& id, quint64 token,
                    const EZTranslator::TranslatedDocument& document);

private:
    QStringList runEngines(const QStringList& units);

    TranslationCache m_cache;
    std::unique_ptr<ITranslator> m_primary;
    std::unique_ptr<ITranslator> m_fallback;
    QString m_source{QStringLiteral("en")};
    QString m_target{QStringLiteral("vi")};
};

} // namespace EZTranslator
