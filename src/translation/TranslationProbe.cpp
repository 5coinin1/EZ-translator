#include "translation/TranslationProbe.h"

#include "translation/AiTranslator.h"
#include "translation/CTranslate2Translator.h"
#include "translation/GoogleTranslator.h"
#include "translation/MarianTranslator.h"

#include <QDir>
#include <QFile>
#include <QStringList>

#include <memory>

namespace EZTranslator::TranslationProbe {

namespace {

QString modelsDir()
{
    return qEnvironmentVariable("EZ_MODELS_DIR", QStringLiteral(EZ_DEFAULT_MODELS_DIR));
}

struct OfflinePair
{
    const char* source;
    const char* target;
    const char* dir;
};

/// Bảng cặp model offline — giữ ĐỒNG BỘ với TranslationWorker::configure.
const OfflinePair kOfflinePairs[] = {
    { "en", "vi", "/ct2-en-vi-int8" },
    { "zh", "vi", "/ct2-zh-vi" },
};

const OfflinePair* findPair(const QString& source, const QString& target)
{
    const QString lookup = source == QLatin1String("auto") ? QStringLiteral("en") : source;
    for (const OfflinePair& pair : kOfflinePairs) {
        if (lookup == QLatin1String(pair.source) && target == QLatin1String(pair.target))
            return &pair;
    }
    return nullptr;
}

bool marianAvailable(const QString& modelDir)
{
    return QFile::exists(modelDir + QStringLiteral("/encoder_model.onnx"))
           && QFile::exists(modelDir + QStringLiteral("/decoder_model.onnx"))
           && QFile::exists(modelDir + QStringLiteral("/source.spm"))
           && QFile::exists(modelDir + QStringLiteral("/target.spm"));
}

Result probeMarian(const QString& source, const QString& target, const QString& sample)
{
    if (source != QLatin1String("en") || target != QLatin1String("vi"))
        return { false, QStringLiteral("marian"),
                 QStringLiteral("Marian ONNX chỉ có model en→vi.") };

    const QString modelDir = modelsDir() + QStringLiteral("/opus-mt-en-vi");
    if (!marianAvailable(modelDir))
        return { false, QStringLiteral("marian"),
                 QStringLiteral("Thiếu file model Marian ở %1.").arg(modelDir) };

    MarianOptions options;
    options.encoderModelPath = modelDir + QStringLiteral("/encoder_model.onnx");
    options.decoderModelPath = modelDir + QStringLiteral("/decoder_model.onnx");
    options.decoderWithPastModelPath =
        modelDir + QStringLiteral("/decoder_with_past_model.onnx");
    options.sourceSpmPath = modelDir + QStringLiteral("/source.spm");
    options.targetSpmPath = modelDir + QStringLiteral("/target.spm");
    options.vocabPath = modelDir + QStringLiteral("/vocab.json");
    options.useGpu = !qEnvironmentVariableIsSet("EZ_MT_CPU");
    options.debug = qEnvironmentVariableIsSet("EZ_MT_DEBUG");

    MarianTranslator marian;
    QString error;
    if (!marian.load(options, &error)) {
        return { false, QStringLiteral("marian"),
                 QStringLiteral("Nạp Marian thất bại: %1").arg(error) };
    }

    const QStringList out = marian.translate({ sample }, source, target);
    if (out.isEmpty() || out.first().trimmed().isEmpty())
        return { false, QStringLiteral("marian"), QStringLiteral("Marian không trả bản dịch.") };

    return { true, QStringLiteral("marian"),
             QStringLiteral("Offline (Marian ONNX) dịch được: \"%1\"").arg(out.first().trimmed()) };
}

Result probeOffline(const TranslationConfig& config, const QString& sample)
{
    const OfflinePair* pair = findPair(config.source, config.target);
    if (!pair) {
        return { false, QStringLiteral("offline"),
                 QStringLiteral("Chưa có model offline cho cặp %1 → %2.")
                     .arg(config.source, config.target) };
    }

    const QString modelDir = modelsDir() + QString::fromLatin1(pair->dir);
    if (!QDir(modelDir).exists()) {
        return { false, QStringLiteral("offline"),
                 QStringLiteral("Thư mục model offline không tồn tại: %1").arg(modelDir) };
    }

    QString ct2Note = QStringLiteral("Python + ctranslate2 chưa được cài (đặt EZ_PYTHON "
                                     "hoặc pip install ctranslate2 sentencepiece).");

    // Ưu tiên 1 — CTranslate2 (nhanh nhất); findPython() chỉ trả về khi import được cả
    // ctranslate2 + sentencepiece nên không cần tự kiểm tra module.
    const QString python = CTranslate2Translator::findPython();
    if (!python.isEmpty()) {
        CTranslate2Translator ct2;
        QString error;
        if (ct2.start(python, QString(), modelDir, QStringLiteral("int8"),
                      QString::fromLatin1(pair->source), QString::fromLatin1(pair->target),
                      &error)) {
            const QStringList out = ct2.translate({ sample },
                                                  QString::fromLatin1(pair->source),
                                                  QString::fromLatin1(pair->target));
            if (!out.isEmpty() && !out.first().trimmed().isEmpty()) {
                return { true, QStringLiteral("ct2"),
                         QStringLiteral("Offline (CTranslate2 int8) dịch được: \"%1\"")
                             .arg(out.first().trimmed()) };
            }
            return { false, QStringLiteral("ct2"),
                     QStringLiteral("Sidecar chạy nhưng không trả bản dịch (xem %1/ez_ct2.log).")
                         .arg(QDir::tempPath()) };
        }
        ct2Note = QStringLiteral("Khởi động CTranslate2 thất bại: %1").arg(error);
    }

    // Ưu tiên 2 — Marian ONNX (không cần Python), hiện chỉ có en→vi.
    const Result marian = probeMarian(config.source, config.target, sample);
    if (marian.ok)
        return marian;

    return { false, QStringLiteral("offline"),
             QStringLiteral("%1 %2").arg(ct2Note, marian.detail) };
}

Result probeGoogle(const TranslationConfig& config, const QString& sample)
{
    GoogleTranslator translator;
    translator.setApiKey(config.googleApiKey);

    const QStringList out = translator.translate({ sample }, config.source, config.target);
    if (out.isEmpty() || out.first().trimmed().isEmpty()) {
        return { false, QStringLiteral("google"),
                 QStringLiteral("Google không trả bản dịch — kiểm tra mạng/api key/quota "
                                "(log có dòng [Google]).") };
    }

    const QString route = config.googleApiKey.trimmed().isEmpty()
                              ? QStringLiteral("endpoint miễn phí")
                              : QStringLiteral("Cloud Translation API");
    return { true, QStringLiteral("google"),
             QStringLiteral("Google (%1) dịch được: \"%2\"")
                 .arg(route, out.first().trimmed()) };
}

Result probeAi(const TranslationConfig& config, const QString& sample)
{
    if (config.aiEndpoint.trimmed().isEmpty() || config.aiModel.trimmed().isEmpty()) {
        return { false, QStringLiteral("ai"),
                 QStringLiteral("Thiếu Endpoint hoặc Model — điền ở mục AI / LLM bên trên.") };
    }

    AiTranslator::Options options;
    options.endpoint = config.aiEndpoint.trimmed();
    options.apiKey = config.aiApiKey.trimmed();
    options.model = config.aiModel.trimmed();
    options.systemPrompt = config.aiSystemPrompt;

    AiTranslator translator(std::move(options));
    const QStringList out = translator.translate({ sample }, config.source, config.target);
    if (out.isEmpty() || out.first().trimmed().isEmpty()) {
        return { false, QStringLiteral("ai"),
                 QStringLiteral("Endpoint không trả bản dịch — kiểm tra URL/key/model "
                                "(log có dòng [AI]).") };
    }

    return { true, QStringLiteral("ai"),
             QStringLiteral("AI (%1) dịch được: \"%2\"")
                 .arg(config.aiModel.trimmed(), out.first().trimmed()) };
}

} // namespace

Result run(const TranslationConfig& config, const QString& sample)
{
    TranslationConfig effective = config;
    if (effective.target.isEmpty())
        effective.target = QStringLiteral("vi");
    if (effective.engine.isEmpty())
        effective.engine = QStringLiteral("auto");

    if (effective.engine == QLatin1String("ai"))
        return probeAi(effective, sample);
    if (effective.engine == QLatin1String("google"))
        return probeGoogle(effective, sample);
    if (effective.engine == QLatin1String("offline"))
        return probeOffline(effective, sample);

    // "auto": giống worker — offline trước, hỏng thì Google đỡ.
    Result offline = probeOffline(effective, sample);
    if (offline.ok)
        return offline;

    Result google = probeGoogle(effective, sample);
    if (google.ok) {
        google.detail = QStringLiteral("Offline không khả dụng (%1) → Google đang gánh: %2")
                            .arg(offline.detail, google.detail);
        return google;
    }

    return { false, QStringLiteral("auto"),
             QStringLiteral("Offline: %1 | Google: %2").arg(offline.detail, google.detail) };
}

} // namespace EZTranslator::TranslationProbe
