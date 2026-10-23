#pragma once

#include "translation/ITranslator.h"

#include <memory>

namespace EZTranslator {

/** Tùy chọn cho engine dịch offline Marian (OPUS-MT, ONNX). */
struct MarianOptions
{
    QString encoderModelPath;
    QString decoderModelPath;
    QString decoderWithPastModelPath; ///< có thì dùng KV-cache (nhanh hơn nhiều trên GPU)
    QString sourceSpmPath;
    QString targetSpmPath;
    QString vocabPath;
    int  threads{0};
    bool useGpu{true};
    int  maxNewTokens{256};
    int  decoderStartTokenId{53684};
    int  eosTokenId{0};
    bool debug{false};
};

/**
 * @brief Dịch offline bằng model OPUS-MT (Marian) dạng ONNX + SentencePiece.
 *
 * Chạy hoàn toàn trên máy, không cần mạng. Dùng encoder + decoder (không KV-cache)
 * để đơn giản; đủ nhanh với câu ngắn (game/truyện).
 */
class MarianTranslator final : public ITranslator
{
public:
    MarianTranslator();
    ~MarianTranslator() override;

    MarianTranslator(const MarianTranslator&) = delete;
    MarianTranslator& operator=(const MarianTranslator&) = delete;

    bool load(const MarianOptions& options, QString* error = nullptr);

    [[nodiscard]] bool isReady() const override;
    [[nodiscard]] QString name() const override { return QStringLiteral("Offline (Marian)"); }

    [[nodiscard]] QStringList translate(const QStringList& lines, const QString& source,
                                        const QString& target) override;

    /** Dịch một dòng (tiện cho hover). */
    [[nodiscard]] QString translateLine(const QString& text);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace EZTranslator
