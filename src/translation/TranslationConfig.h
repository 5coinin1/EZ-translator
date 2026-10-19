#pragma once

#include <QMetaType>
#include <QString>

namespace EZTranslator {

/**
 * @brief Cấu hình engine dịch, được truyền từ AppController sang worker thread.
 *
 * Tách khỏi AppSettings để worker không phụ thuộc vào UI/core types và có thể
 * truyền qua QueuedConnection bằng QMetaObject::invokeMethod.
 */
struct TranslationConfig
{
    QString source{"en"};
    QString target{"vi"};

    /// "auto" = offline → Google; "offline" = chỉ model trên máy;
    /// "google" = Google Translate; "ai" = LLM (OpenAI-compatible).
    QString engine{"auto"};

    QString googleApiKey; ///< rỗng → endpoint gtx miễn phí

    QString aiEndpoint;
    QString aiApiKey;
    QString aiModel;
    QString aiSystemPrompt;

    /// Sức chứa cache bản dịch (số dòng) — Cài đặt → Dịch thuật.
    int cacheCapacity{5000};
};

} // namespace EZTranslator

Q_DECLARE_METATYPE(EZTranslator::TranslationConfig)
