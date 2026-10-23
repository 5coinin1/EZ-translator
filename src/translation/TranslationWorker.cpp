#include "translation/TranslationWorker.h"

#include "translation/AiTranslator.h"
#include "translation/CTranslate2Translator.h"
#include "translation/GoogleTranslator.h"
#include "translation/MarianTranslator.h"
#include "translation/MyMemoryTranslator.h"
#include "translation/TranslationThreadBudget.h"
#include "ocr/OcrTypes.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>

#include <algorithm>

namespace EZTranslator {

namespace {

QString modelsDir()
{
    return qEnvironmentVariable("EZ_MODELS_DIR", QStringLiteral(EZ_DEFAULT_MODELS_DIR));
}

/** Đọc output của một engine vào `out`, chỉ điền các unit còn trống. */
void fillMissing(QStringList& out, const QStringList& units, ITranslator* engine,
                 const QString& source, const QString& target)
{
    if (!engine || !engine->isReady())
        return;

    const QStringList part = engine->translate(units, source, target);
    for (int i = 0; i < part.size() && i < out.size(); ++i) {
        if (out.at(i).isEmpty() && !part.at(i).isEmpty())
            out[i] = part.at(i);
    }
}

bool hasMissing(const QStringList& out, const QStringList& units)
{
    for (int i = 0; i < units.size(); ++i) {
        if (out.at(i).isEmpty() && !units.at(i).trimmed().isEmpty())
            return true;
    }
    return false;
}

} // namespace

TranslationWorker::TranslationWorker(QObject* parent)
    : QObject(parent)
{
}

TranslationWorker::~TranslationWorker() = default;

void TranslationWorker::configure(const TranslationConfig& config)
{
    // Đổi engine/API/model/system prompt → bản dịch cũ trong cache không còn đúng
    // "phong cách" nữa; xoá để lần OCR kế tiếp dùng engine mới ngay.
    const bool engineChanged =
        m_config.engine != config.engine
        || m_config.googleApiKey != config.googleApiKey
        || m_config.aiEndpoint != config.aiEndpoint
        || m_config.aiApiKey != config.aiApiKey
        || m_config.aiModel != config.aiModel
        || m_config.aiSystemPrompt != config.aiSystemPrompt;

    m_config = config;
    if (engineChanged)
        m_cache.clear();
    if (m_config.cacheCapacity > 0)
        m_cache.setLimit(m_config.cacheCapacity);
    if (m_config.target.isEmpty())
        m_config.target = QStringLiteral("vi");
    if (m_config.engine.isEmpty())
        m_config.engine = QStringLiteral("auto");

    const QString engine = m_config.engine;
    const bool allowOffline = engine == QLatin1String("auto")
                              || engine == QLatin1String("offline");

    // EZ_TRANSLATE_ENGINE=online → bỏ qua engine offline (dùng Google/MyMemory);
    // =marian → bỏ qua ct2 (để A/B hai engine offline).
    const bool allowCt2 = qEnvironmentVariable("EZ_TRANSLATE_ENGINE",
                                               QStringLiteral("auto"))
                          != QLatin1String("marian");
    const bool englishToVietnamese = m_config.source == QLatin1String("en")
                                     && m_config.target == QLatin1String("vi");
    const QString dir = modelsDir();

    // BẢNG MODEL OFFLINE THEO CẶP (giống translation_models.json của bản translator): thêm cặp =
    // thêm một dòng ở đây, không sửa gì khác. Model Marian-style (source.spm/target.spm) chạy qua
    // cùng sidecar ct2. Nguồn "auto" (chưa nhận diện) nạp trước cặp en→vi — cold start vài giây
    // không rơi vào bản dịch đầu; nhận diện xong đổi cặp thì sidecar tự khởi động lại với model mới.
    struct OfflinePair
    {
        const char* source;
        const char* target;
        const char* dir;
    };
    static const OfflinePair kOfflinePairs[] = {
        { "en", "vi", "/ct2-en-vi-int8" },
        { "zh", "vi", "/ct2-zh-vi" },
        { "ja", "vi", "/ct2-ja-vi" },
    };
    const QString lookupSource =
        m_config.source == QLatin1String("auto") ? QStringLiteral("en") : m_config.source;
    const OfflinePair* offline = nullptr;
    for (const OfflinePair& pair : kOfflinePairs) {
        if (lookupSource == QLatin1String(pair.source)
            && m_config.target == QLatin1String(pair.target)) {
            offline = &pair;
            break;
        }
    }

    // Ưu tiên 1 — CTranslate2 int8: nhanh gấp ~5 lần Marian ONNX (đo: ~15-40ms/câu
    // so với ~98ms), lại có beam search nên chất lượng tốt hơn.
    if (allowOffline && allowCt2 && offline) {
        const QString modelDir = dir + QString::fromLatin1(offline->dir);
        // Đổi CẶP = đổi model: bỏ sidecar cũ, nếu không nó vẫn phục vụ model cũ (im lặng sai).
        if (m_ct2 && m_ct2ModelDir != modelDir) {
            m_ct2.reset();
            m_ct2ModelDir.clear();
        }
        if (!m_ct2 && QDir(modelDir).exists()) {
            const QString python = CTranslate2Translator::findPython();
            if (python.isEmpty()) {
                qWarning() << "[Translate] không có Python + ctranslate2 → bỏ qua engine ct2";
            } else {
                auto ct2 = std::make_unique<CTranslate2Translator>();
                QString error;
                // Truyền ĐÚNG cặp của MODEL thay vì m_config.source ("auto"/"en"): lần configure
                // sau (auto → en sau nhận diện) không bị coi là đổi ngôn ngữ rồi khởi động lại
                // sidecar — mất toàn bộ công nạp sẵn.
                if (ct2->start(python, QString(), modelDir, QStringLiteral("int8"),
                               QString::fromLatin1(offline->source),
                               QString::fromLatin1(offline->target), &error)) {
                    m_ct2 = std::move(ct2);
                    m_ct2ModelDir = modelDir;
                    qInfo() << "[Translate] engine ct2 sẵn sàng" << offline->source << "->"
                            << offline->target << "(python =" << python << ")";
                } else {
                    qWarning() << "[Translate] ct2 không khởi động được:" << error;
                }
            }
        }
    }

    // Ưu tiên 2 — Marian ONNX (không cần Python), chỉ có en→vi. CHỈ nạp khi ct2 không có: Marian là
    // lưới an toàn, nạp song song cả hai tốn ~100-150MB RAM + ~1s nạp mà gần như không bao giờ dùng
    // (người dùng phản ánh app ăn RAM). ct2 hỏng lúc chạy thì Google đỡ (engine "auto").
    if (allowOffline && englishToVietnamese && !m_marian && !m_ct2) {
        const QString modelDir = dir + QStringLiteral("/opus-mt-en-vi");
        if (QFile::exists(modelDir + QStringLiteral("/encoder_model.onnx"))) {
            MarianOptions options;
            options.encoderModelPath = modelDir + QStringLiteral("/encoder_model.onnx");
            options.decoderModelPath = modelDir + QStringLiteral("/decoder_model.onnx");
            options.decoderWithPastModelPath =
                modelDir + QStringLiteral("/decoder_with_past_model.onnx");
            options.sourceSpmPath = modelDir + QStringLiteral("/source.spm");
            options.targetSpmPath = modelDir + QStringLiteral("/target.spm");
            options.vocabPath = modelDir + QStringLiteral("/vocab.json");
            options.threads = chooseTranslationThreads();
            options.useGpu = !qEnvironmentVariableIsSet("EZ_MT_CPU");
            options.debug = qEnvironmentVariableIsSet("EZ_MT_DEBUG");

            auto marian = std::make_unique<MarianTranslator>();
            QString error;
            if (marian->load(options, &error)) {
                m_marian = std::move(marian);
                qInfo() << "[Translate] engine Marian ONNX sẵn sàng (opus-mt-en-vi)";
            } else {
                qWarning() << "[Translate] nạp Marian thất bại:" << error;
            }
        }
    }

    // Chuỗi offline chỉ dùng model ĐÚNG CẶP hiện tại (nếu không sẽ dịch sang sai ngôn ngữ đích).
    m_offlineChain.clear();
    if (offline && m_ct2 && m_ct2ModelDir == dir + QString::fromLatin1(offline->dir))
        m_offlineChain.append(m_ct2.get());
    if (englishToVietnamese && m_marian)
        m_offlineChain.append(m_marian.get());

    if (!m_primary)
        m_primary = std::make_unique<GoogleTranslator>();
    if (auto* google = dynamic_cast<GoogleTranslator*>(m_primary.get()))
        google->setApiKey(m_config.googleApiKey);

    if (!m_fallback)
        m_fallback = std::make_unique<MyMemoryTranslator>();

    // AI chỉ được tạo khi người dùng chọn engine AI và đã cấu hình endpoint/model.
    if (engine == QLatin1String("ai")) {
        AiTranslator::Options options;
        options.endpoint = m_config.aiEndpoint.trimmed();
        options.apiKey = m_config.aiApiKey.trimmed();
        options.model = m_config.aiModel.trimmed();
        options.systemPrompt = m_config.aiSystemPrompt;

        if (!options.endpoint.isEmpty() && !options.model.isEmpty()) {
            m_ai = std::make_unique<AiTranslator>(std::move(options));
            qInfo() << "[Translate] engine AI sẵn sàng, model =" << m_config.aiModel;
        } else {
            m_ai.reset();
            qWarning() << "[Translate] chọn AI nhưng thiếu Endpoint hoặc Model";
        }
    } else {
        m_ai.reset();
    }

    qInfo() << "[Translate] cấu hình: engine =" << engine
            << "| source =" << m_config.source
            << "| offline =" << m_offlineChain.size()
            << "| googleKey =" << (!m_config.googleApiKey.isEmpty());
}

QStringList TranslationWorker::runEngines(const QStringList& units)
{
    QStringList out;
    out.resize(units.size()); // rỗng = unit đó lỗi

    const QString mode = m_config.engine.isEmpty() ? QStringLiteral("auto") : m_config.engine;

    // Chuỗi engine OFFLINE theo thứ tự ưu tiên: engine sau chỉ chạy cho unit mà
    // engine trước bỏ trống (vd ct2 chết → Marian đỡ).
    if (mode == QLatin1String("auto") || mode == QLatin1String("offline")) {
        for (ITranslator* engine : m_offlineChain) {
            fillMissing(out, units, engine, m_config.source, m_config.target);
            if (!hasMissing(out, units))
                break;
        }

        if (mode == QLatin1String("offline"))
            return out; // người dùng chọn offline: không chạm mạng

        // Engine được ghi rõ trong Settings là "Offline → Google": nếu model offline
        // bỏ trống một unit (không hỗ trợ cặp ngôn ngữ, lỗi tokenizer/model, output bị
        // TranslationPost lọc) thì phải cho Google thử unit đó. Trước đây chỉ cần có
        // model offline là return ngay cả khi output rỗng, nên overlay mãi hiện OCR gốc.
        if (!m_offlineChain.isEmpty() && hasMissing(out, units)) {
            qInfo() << "[Translate] offline để trống unit; thử Google fallback"
                    << "units=" << units.size();
        }
    }

    // AI đi trước khi người dùng chọn engine AI; sau đó Google/MyMemory đỡ các
    // đoạn AI bỏ sót để pipeline không bị trống.
    if (mode == QLatin1String("ai"))
        fillMissing(out, units, m_ai.get(), m_config.source, m_config.target);

    if (!hasMissing(out, units))
        return out;

    fillMissing(out, units, m_primary.get(), m_config.source, m_config.target);

    if (!hasMissing(out, units))
        return out;

    fillMissing(out, units, m_fallback.get(), m_config.source, m_config.target);
    return out;
}

void TranslationWorker::translate(const QString& id, const QString& text, quint64 token,
                                  qint64 queuedAtMs)
{
    enqueueRequest(PendingRequest{id, text, token, queuedAtMs, false});
}

void TranslationWorker::translateInteractive(const QString& id, const QString& text, quint64 token,
                                             qint64 queuedAtMs)
{
    PendingRequest request{id, text, token, queuedAtMs, true};
    m_interactivePending.store(true);

    // Khi đang dịch vùng, cho hover chen ngay vào giữa các chunk. Các hover tiếp theo
    // trong chính lượt hover chỉ giữ bản mới nhất để tránh đệ quy không giới hạn.
    if (m_processing && !m_processingInteractive) {
        m_processingInteractive = true;
        handleTranslate(request.id, request.text, request.token, true);
        m_processingInteractive = false;
        drainRequests();
        return;
    }

    m_pendingInteractive = std::move(request);
    if (!m_processing)
        drainRequests();
}

void TranslationWorker::enqueueRequest(PendingRequest request)
{
    if (request.interactive) {
        m_pendingInteractive = std::move(request);
        m_interactivePending.store(true);
    } else {
        const QString id = request.id;
        const bool wasPending = m_pendingRegions.contains(id);
        m_pendingRegions.insert(id, std::move(request));
        if (!wasPending)
            m_regionOrder.enqueue(id);
    }

    if (!m_processing)
        drainRequests();
}

void TranslationWorker::drainRequests()
{
    if (m_processing)
        return;

    m_processing = true;
    while (m_pendingInteractive.has_value() || !m_regionOrder.isEmpty()) {
        PendingRequest request;
        if (m_pendingInteractive.has_value()) {
            request = std::move(*m_pendingInteractive);
            m_pendingInteractive.reset();
        } else {
            const QString id = m_regionOrder.dequeue();
            auto it = m_pendingRegions.find(id);
            if (it == m_pendingRegions.end())
                continue;
            request = std::move(it.value());
            m_pendingRegions.erase(it);
        }

        const qint64 startedAtMs = ocrMonotonicMs();
        handleTranslate(request.id, request.text, request.token, request.interactive);
        qInfo().noquote() << "[TRANSLATE_METRIC] id=" << request.id
                          << "interactive=" << request.interactive
                          << "queueMs=" << (request.queuedAtMs > 0
                                                  ? startedAtMs - request.queuedAtMs
                                                  : -1);
    }
    m_processing = false;
}

void TranslationWorker::handleTranslate(const QString& id, const QString& text, quint64 token,
                                        bool interactive)
{
    if (interactive)
        m_interactivePending.store(false);

    if (!m_primary)
        configure(m_config);

    // Chia khối: giữa các khối, nếu có yêu cầu hover đang chờ thì nhường event loop
    // cho nó chạy xong trước — lượt dịch vùng dài không giữ luồng tới hết trang.
    // AI nhận khối lớn hơn để có ngữ cảnh và giảm số lần gọi API.
    // Offline CT2 nhận cả batch và tự tối ưu translate_batch. Nhóm lớn giảm số lần
    // IPC/chờ sidecar; online vẫn giữ nhóm nhỏ để không làm một lượt mạng quá lâu.
    const int chunkSize = m_config.engine == QLatin1String("ai")
                              ? 8
                              : (!m_offlineChain.isEmpty() ? 16 : 4);
    const auto engine = [this, chunkSize](const QStringList& units) -> QStringList {
        QStringList out;
        out.reserve(units.size());
        for (int start = 0; start < units.size(); start += chunkSize) {
            out += runEngines(units.mid(start, chunkSize));
            if (m_interactivePending.load() && start + chunkSize < units.size())
                QCoreApplication::processEvents(QEventLoop::AllEvents, 0);
        }
        return out;
    };

    DocumentTranslator translator(&m_cache);

    // EMIT #1 — phần ĐÃ CÓ trong cache, KHÔNG gọi engine: overlay hiện NGAY ở vị trí chính xác với
    // những gì đã dịch được; phần chưa có để trống rồi được điền tiếp ở emit #2 (cùng dòng ⇒ cùng
    // vị trí). Bài học translator: "emit cached right away, then translate the missing units —
    // waiting for the translation pass first made the re-show feel 200-400 ms slower for no
    // positional benefit". Chỉ áp cho lượt DỊCH VÙNG; hover giữ một phát (popup cần đủ nguồn+dịch).
    if (!interactive) {
        const TranslatedDocument cachedDocument =
            translator.translate(text, m_config.source, m_config.target, nullptr);
        bool anyCached = false;
        for (const QString& paragraph : cachedDocument.paragraphs) {
            if (!paragraph.isEmpty()) {
                anyCached = true;
                break;
            }
        }
        if (anyCached)
            emit translated(id, token, cachedDocument);
    }

    const qint64 startedAtMs = ocrMonotonicMs();
    const TranslatedDocument document =
        translator.translate(text, m_config.source, m_config.target, engine);

    emit translated(id, token, document);
    qInfo().noquote() << "[TRANSLATE_METRIC] id=" << id
                      << "interactive=" << interactive
                      << "totalMs=" << (ocrMonotonicMs() - startedAtMs)
                      << "new=" << document.newTranslations
                      << "complete=" << document.complete;
}

} // namespace EZTranslator
