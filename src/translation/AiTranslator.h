#pragma once

#include "translation/ITranslator.h"

#include <memory>

namespace EZTranslator {

/**
 * @brief Engine dịch qua LLM dùng API tương thích OpenAI Chat Completions.
 *
 * Hỗ trợ OpenAI, OpenRouter, DeepSeek, Groq, Ollama, LM Studio, vLLM… bất kỳ
 * endpoint nào nhận `POST /v1/chat/completions` với body `{model, messages}`.
 *
 * System prompt của người dùng được gửi kèm để model hiểu ngữ cảnh (game, truyện,
 * cách xưng hô…). Ngoài ra engine tự thêm quy tắc đầu ra JSON để ghép bản dịch
 * theo đúng đoạn.
 */
class AiTranslator final : public ITranslator
{
public:
    struct Options
    {
        QString endpoint;
        QString apiKey;
        QString model;
        QString systemPrompt;
    };

    explicit AiTranslator(Options options);
    ~AiTranslator() override;

    AiTranslator(const AiTranslator&) = delete;
    AiTranslator& operator=(const AiTranslator&) = delete;

    [[nodiscard]] bool isReady() const override;
    [[nodiscard]] QString name() const override { return QStringLiteral("AI"); }

    [[nodiscard]] QStringList translate(const QStringList& lines, const QString& source,
                                        const QString& target) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace EZTranslator
