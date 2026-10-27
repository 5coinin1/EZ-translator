#include "translation/MarianTranslator.h"

#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVector>

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

// MinGW: header ORT dùng `_stdcall` (chỉ MSVC có).
#if defined(__MINGW32__) && !defined(_stdcall)
#define _stdcall __stdcall
#endif
#if defined(_WIN32) && !defined(_Frees_ptr_opt_)
#define _Frees_ptr_opt_
#endif

#include <onnxruntime_cxx_api.h>
#include <sentencepiece_processor.h>

namespace EZTranslator {

namespace {

int nameIndex(const std::vector<std::string>& names, const std::string& needle)
{
    for (int i = 0; i < int(names.size()); ++i)
        if (names[size_t(i)] == needle)
            return i;
    return -1;
}

const char* kPastPrefix = "past_key_values.";
const char* kPresentPrefix = "present.";

std::string presentNameFor(const std::string& pastName)
{
    if (pastName.rfind(kPastPrefix, 0) == 0)
        return std::string(kPresentPrefix)
               + pastName.substr(std::char_traits<char>::length(kPastPrefix));
    return {};
}

bool isEncoderKeyValue(const std::string& pastName)
{
    return pastName.find(".encoder.") != std::string::npos;
}

} // namespace

struct MarianTranslator::Impl
{
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "ez-translator-mt"};
    std::unique_ptr<Ort::Session> encoder;
    std::unique_ptr<Ort::Session> decoder;
    std::unique_ptr<Ort::Session> decoderWithPast;
    std::vector<std::string> encoderInputs;
    std::vector<std::string> encoderOutputs;
    std::vector<std::string> decoderInputs;
    std::vector<std::string> decoderOutputs;
    std::vector<std::string> pastInputs;
    std::vector<std::string> pastOutputs;
    sentencepiece::SentencePieceProcessor source;
    sentencepiece::SentencePieceProcessor target;
    QHash<QString, int> pieceToId;
    QVector<QString> idToPiece;
    MarianOptions options;
    bool loaded{false};

    bool createSessions(bool useGpu)
    {
        try {
            Ort::SessionOptions sessionOptions;
            sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
            if (options.threads > 0)
                sessionOptions.SetIntraOpNumThreads(options.threads);
            if (useGpu) {
                sessionOptions.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
#if defined(_WIN32)
                using AppendFn = OrtStatus* (*)(OrtSessionOptions*, int);
                if (HMODULE module = GetModuleHandleW(L"onnxruntime.dll")) {
                    if (auto append = reinterpret_cast<AppendFn>(
                            GetProcAddress(module, "OrtSessionOptionsAppendExecutionProvider_DML"))) {
                        if (OrtStatus* status = append(sessionOptions, 0)) {
                            const char* message = Ort::GetApi().GetErrorMessage(status);
                            const QString detail =
                                QString::fromUtf8(message ? message : "DML failed");
                            Ort::GetApi().ReleaseStatus(status);
                            throw std::runtime_error(detail.toStdString());
                        }
                    }
                }
#endif
            }

            encoder = std::make_unique<Ort::Session>(
                env, options.encoderModelPath.toStdWString().c_str(), sessionOptions);
            decoder = std::make_unique<Ort::Session>(
                env, options.decoderModelPath.toStdWString().c_str(), sessionOptions);
            if (!options.decoderWithPastModelPath.isEmpty()) {
                decoderWithPast = std::make_unique<Ort::Session>(
                    env, options.decoderWithPastModelPath.toStdWString().c_str(), sessionOptions);
            }
            return true;
        } catch (const std::exception&) {
            encoder.reset();
            decoder.reset();
            decoderWithPast.reset();
            return false;
        }
    }
};

MarianTranslator::MarianTranslator()
    : m_impl(std::make_unique<Impl>())
{
}

MarianTranslator::~MarianTranslator() = default;

bool MarianTranslator::isReady() const
{
    return m_impl && m_impl->loaded;
}

bool MarianTranslator::load(const MarianOptions& options, QString* error)
{
    if (!m_impl)
        return false;
    m_impl->options = options;

    const auto sourceStatus = m_impl->source.Load(options.sourceSpmPath.toStdString());
    const auto targetStatus = m_impl->target.Load(options.targetSpmPath.toStdString());
    if (!sourceStatus.ok() || !targetStatus.ok()) {
        if (error)
            *error = QStringLiteral("Không nạp được SentencePiece model.");
        return false;
    }

    if (!options.vocabPath.isEmpty()) {
        QFile file(options.vocabPath);
        if (!file.open(QIODevice::ReadOnly)) {
            if (error)
                *error = QStringLiteral("Không mở được vocab.json.");
            return false;
        }
        const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
        int maxId = 0;
        for (auto it = object.begin(); it != object.end(); ++it) {
            const int id = it.value().toInt();
            m_impl->pieceToId.insert(it.key(), id);
            maxId = qMax(maxId, id);
        }
        m_impl->idToPiece.resize(maxId + 1);
        for (auto it = object.begin(); it != object.end(); ++it)
            m_impl->idToPiece[it.value().toInt()] = it.key();
    }

    if (!m_impl->createSessions(options.useGpu) && !m_impl->createSessions(false)) {
        if (error)
            *error = QStringLiteral("Không tạo được ONNX session cho translation.");
        return false;
    }

    try {
        Ort::AllocatorWithDefaultOptions allocator;
        const auto collect = [&allocator](Ort::Session& session, size_t count,
                                          std::vector<std::string>& sink, bool inputs) {
            for (size_t i = 0; i < count; ++i) {
                auto name = inputs ? session.GetInputNameAllocated(i, allocator)
                                   : session.GetOutputNameAllocated(i, allocator);
                sink.emplace_back(name.get());
            }
        };
        collect(*m_impl->encoder, m_impl->encoder->GetInputCount(), m_impl->encoderInputs, true);
        collect(*m_impl->encoder, m_impl->encoder->GetOutputCount(), m_impl->encoderOutputs, false);
        collect(*m_impl->decoder, m_impl->decoder->GetInputCount(), m_impl->decoderInputs, true);
        collect(*m_impl->decoder, m_impl->decoder->GetOutputCount(), m_impl->decoderOutputs, false);
        if (m_impl->decoderWithPast) {
            collect(*m_impl->decoderWithPast, m_impl->decoderWithPast->GetInputCount(),
                    m_impl->pastInputs, true);
            collect(*m_impl->decoderWithPast, m_impl->decoderWithPast->GetOutputCount(),
                    m_impl->pastOutputs, false);
        }
    } catch (const std::exception& exception) {
        if (error)
            *error = QString::fromUtf8(exception.what());
        return false;
    }

    m_impl->loaded = true;

    // Làm nóng: lần suy luận đầu tiên phải biên dịch graph (chậm hơn nhiều) — chạy sẵn
    // một câu ngắn để câu dịch thật đầu tiên không bị khựng.
    translateLine(QStringLiteral("Hello"));

    return true;
}

QString MarianTranslator::translateLine(const QString& text)
{
    if (!m_impl || !m_impl->loaded || text.trimmed().isEmpty())
        return {};

    Impl& impl = *m_impl;
    const Ort::MemoryInfo memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    // Tokenize → map piece sang id của model (id vocab khác id spm thô với Marian).
    std::vector<std::string> pieces;
    impl.source.Encode(text.toStdString(), &pieces);
    if (pieces.empty())
        return {};

    std::vector<int> sourceIds;
    sourceIds.reserve(pieces.size() + 1);
    for (const std::string& piece : pieces) {
        const auto it = impl.pieceToId.constFind(QString::fromStdString(piece));
        sourceIds.push_back(it != impl.pieceToId.constEnd() ? it.value() : 1 /*<unk>*/);
    }
    sourceIds.push_back(impl.options.eosTokenId);

    try {
        const int64_t sourceLength = int64_t(sourceIds.size());
        std::vector<int64_t> sourceTensor(sourceIds.begin(), sourceIds.end());
        std::vector<int64_t> sourceMask(size_t(sourceLength), 1);
        std::vector<int64_t> sourceShape = {1, sourceLength};

        // ── Encoder ──────────────────────────────────────────────────────────
        std::vector<Ort::Value> encoderInputs;
        std::vector<const char*> encoderInputNames;
        for (const std::string& name : impl.encoderInputs) {
            if (name == "input_ids") {
                encoderInputs.push_back(Ort::Value::CreateTensor<int64_t>(
                    memoryInfo, sourceTensor.data(), sourceTensor.size(), sourceShape.data(), 2));
            } else if (name == "attention_mask") {
                encoderInputs.push_back(Ort::Value::CreateTensor<int64_t>(
                    memoryInfo, sourceMask.data(), sourceMask.size(), sourceShape.data(), 2));
            } else {
                return {};
            }
            encoderInputNames.push_back(name.c_str());
        }
        std::vector<const char*> encoderOutputNames;
        for (const std::string& name : impl.encoderOutputs)
            encoderOutputNames.push_back(name.c_str());

        auto encoderResult = impl.encoder->Run(Ort::RunOptions{nullptr}, encoderInputNames.data(),
                                               encoderInputs.data(), encoderInputs.size(),
                                               encoderOutputNames.data(), encoderOutputNames.size());
        if (encoderResult.empty())
            return {};

        Ort::Value& hidden = encoderResult[0];
        const auto hiddenShape = hidden.GetTensorTypeAndShapeInfo().GetShape();
        float* hiddenData = hidden.GetTensorMutableData<float>();
        const size_t hiddenCount =
            size_t(hiddenShape[0]) * size_t(hiddenShape[1]) * size_t(hiddenShape[2]);

        const int logitsIndex = nameIndex(impl.decoderOutputs, "logits");
        const int pastLogitsIndex =
            impl.decoderWithPast ? nameIndex(impl.pastOutputs, "logits") : -1;
        if (logitsIndex < 0)
            return {};

        const auto argmax = [](const float* row, int classes) {
            int best = 0;
            float bestValue = row[0];
            for (int c = 1; c < classes; ++c) {
                if (row[c] > bestValue) {
                    bestValue = row[c];
                    best = c;
                }
            }
            return best;
        };

        std::vector<int64_t> generated = {impl.options.decoderStartTokenId};
        std::vector<Ort::Value> step0Outputs;  // decoder (no past): logits + KV (encoder + decoder)
        std::vector<Ort::Value> latestOutputs; // with_past: logits + decoder KV
        bool finished = false;

        // Chặn trường hợp greedy lặp vô hạn làm hover khựng vài giây: số bước tối đa
        // tỉ lệ với độ dài câu nguồn (thoải mái đủ cho bản dịch dài nhất).
        const int maxSteps = qMin(impl.options.maxNewTokens, int(sourceIds.size()) * 4 + 16);

        // Bước 0: decoder không-KV với token start (shape {1,1} — cố định).
        {
            std::vector<int64_t> decoderTensor = {impl.options.decoderStartTokenId};
            std::vector<int64_t> decoderShape = {1, 1};

            std::vector<Ort::Value> inputs;
            std::vector<const char*> inputNames;
            for (const std::string& name : impl.decoderInputs) {
                if (name == "input_ids") {
                    inputs.push_back(Ort::Value::CreateTensor<int64_t>(
                        memoryInfo, decoderTensor.data(), decoderTensor.size(), decoderShape.data(),
                        2));
                } else if (name == "encoder_attention_mask") {
                    inputs.push_back(Ort::Value::CreateTensor<int64_t>(
                        memoryInfo, sourceMask.data(), sourceMask.size(), sourceShape.data(), 2));
                } else if (name == "encoder_hidden_states") {
                    inputs.push_back(Ort::Value::CreateTensor<float>(
                        memoryInfo, hiddenData, hiddenCount, hiddenShape.data(),
                        int(hiddenShape.size())));
                } else {
                    return {};
                }
                inputNames.push_back(name.c_str());
            }

            std::vector<const char*> outputNames;
            for (const std::string& name : impl.decoderOutputs)
                outputNames.push_back(name.c_str());

            step0Outputs = impl.decoder->Run(Ort::RunOptions{nullptr}, inputNames.data(),
                                             inputs.data(), inputs.size(), outputNames.data(),
                                             outputNames.size());
            if (step0Outputs.empty())
                return {};

            const auto shape = step0Outputs[logitsIndex].GetTensorTypeAndShapeInfo().GetShape();
            const int steps = int(shape[1]);
            const int classes = int(shape[2]);
            const float* row = step0Outputs[logitsIndex].GetTensorMutableData<float>()
                               + size_t(steps - 1) * size_t(classes);
            const int next = argmax(row, classes);
            finished = (next == impl.options.eosTokenId);
            if (!finished)
                generated.push_back(next);
        }

        const bool usePast = impl.decoderWithPast && pastLogitsIndex >= 0;

        // ── Các bước tiếp theo: dùng KV-cache → mỗi bước shape {1,1} (không recompile) ──
        for (int step = 1; !finished && step < maxSteps; ++step) {
            int next = impl.options.eosTokenId;

            const auto runNoPast = [&]() -> bool {
                std::vector<int64_t> decoderShape = {1, int64_t(generated.size())};
                std::vector<Ort::Value> inputs;
                std::vector<const char*> inputNames;
                for (const std::string& name : impl.decoderInputs) {
                    if (name == "input_ids") {
                        inputs.push_back(Ort::Value::CreateTensor<int64_t>(
                            memoryInfo, generated.data(), generated.size(), decoderShape.data(), 2));
                    } else if (name == "encoder_attention_mask") {
                        inputs.push_back(Ort::Value::CreateTensor<int64_t>(
                            memoryInfo, sourceMask.data(), sourceMask.size(), sourceShape.data(), 2));
                    } else if (name == "encoder_hidden_states") {
                        inputs.push_back(Ort::Value::CreateTensor<float>(
                            memoryInfo, hiddenData, hiddenCount, hiddenShape.data(),
                            int(hiddenShape.size())));
                    } else {
                        return false;
                    }
                    inputNames.push_back(name.c_str());
                }
                std::vector<const char*> outputNames;
                for (const std::string& name : impl.decoderOutputs)
                    outputNames.push_back(name.c_str());
                auto result = impl.decoder->Run(Ort::RunOptions{nullptr}, inputNames.data(),
                                                inputs.data(), inputs.size(), outputNames.data(),
                                                outputNames.size());
                if (result.empty())
                    return false;
                const auto shape = result[logitsIndex].GetTensorTypeAndShapeInfo().GetShape();
                const int steps = int(shape[1]);
                const int classes = int(shape[2]);
                const float* row = result[logitsIndex].GetTensorMutableData<float>()
                                   + size_t(steps - 1) * size_t(classes);
                next = argmax(row, classes);
                step0Outputs = std::move(result);
                return true;
            };

            bool handled = false;
            if (usePast) {
                std::vector<int64_t> token = {generated.back()};
                std::vector<int64_t> tokenShape = {1, 1};

                std::vector<Ort::Value> inputs;
                std::vector<const char*> inputNames;
                bool ok = true;
                for (const std::string& name : impl.pastInputs) {
                    if (name == "input_ids") {
                        inputs.push_back(Ort::Value::CreateTensor<int64_t>(
                            memoryInfo, token.data(), token.size(), tokenShape.data(), 2));
                    } else if (name.rfind(kPastPrefix, 0) == 0) {
                        const std::string present = presentNameFor(name);
                        // Encoder KV không đổi → lấy từ bước 0; decoder KV nối từ bước trước.
                        const bool encoder = isEncoderKeyValue(name);
                        std::vector<Ort::Value>& src =
                            (encoder || latestOutputs.empty()) ? step0Outputs : latestOutputs;
                        const std::vector<std::string>& names =
                            (encoder || latestOutputs.empty()) ? impl.decoderOutputs
                                                               : impl.pastOutputs;
                        const int idx = nameIndex(names, present);
                        if (idx < 0 || idx >= int(src.size())) {
                            ok = false;
                            break;
                        }
                        const auto info = src[idx].GetTensorTypeAndShapeInfo();
                        const auto shape = info.GetShape();
                        inputs.push_back(Ort::Value::CreateTensor<float>(
                            memoryInfo, src[idx].GetTensorMutableData<float>(),
                            info.GetElementCount(), shape.data(), int(shape.size())));
                    } else if (name == "encoder_attention_mask") {
                        inputs.push_back(Ort::Value::CreateTensor<int64_t>(
                            memoryInfo, sourceMask.data(), sourceMask.size(), sourceShape.data(),
                            2));
                    } else if (name == "encoder_hidden_states") {
                        inputs.push_back(Ort::Value::CreateTensor<float>(
                            memoryInfo, hiddenData, hiddenCount, hiddenShape.data(),
                            int(hiddenShape.size())));
                    } else {
                        ok = false;
                        break;
                    }
                    inputNames.push_back(name.c_str());
                }

                if (ok) {
                    std::vector<const char*> outputNames;
                    for (const std::string& name : impl.pastOutputs)
                        outputNames.push_back(name.c_str());
                    auto result = impl.decoderWithPast->Run(
                        Ort::RunOptions{nullptr}, inputNames.data(), inputs.data(), inputs.size(),
                        outputNames.data(), outputNames.size());
                    if (!result.empty()) {
                        const auto shape =
                            result[pastLogitsIndex].GetTensorTypeAndShapeInfo().GetShape();
                        const int steps = int(shape[1]);
                        const int classes = int(shape[2]);
                        const float* row = result[pastLogitsIndex].GetTensorMutableData<float>()
                                           + size_t(steps - 1) * size_t(classes);
                        next = argmax(row, classes);
                        latestOutputs = std::move(result);
                        handled = true;
                    }
                }
            }

            if (!handled && !runNoPast())
                break;
            if (next == impl.options.eosTokenId)
                break;
            generated.push_back(next);
        }

        if (impl.options.debug) {
            QStringList src;
            for (const std::string& piece : pieces)
                src << (QString::fromStdString(piece) + QLatin1Char('=')
                        + QString::number(impl.pieceToId.value(QString::fromStdString(piece), -1)));
            QStringList gen;
            for (size_t i = 1; i < generated.size(); ++i) {
                const int id = int(generated[i]);
                gen << (QString::number(id) + QLatin1Char(':')
                        + (id >= 0 && id < impl.idToPiece.size() ? impl.idToPiece.at(id)
                                                                 : QStringLiteral("<oob>")));
            }
            qInfo().noquote() << "[mt dbg] src" << src.join(QLatin1Char(' '));
            qInfo().noquote() << "[mt dbg] gen" << gen.join(QLatin1Char(' '));
        }

        // Ghép mảnh → text.
        std::vector<std::string> outPieces;
        for (size_t i = 1; i < generated.size(); ++i) {
            const int id = int(generated[i]);
            if (id <= 0 || id == impl.options.decoderStartTokenId || id >= impl.idToPiece.size())
                continue;
            const QString piece = impl.idToPiece.at(id);
            if (piece.isEmpty() || piece.startsWith(QLatin1String(">>"))
                || piece.startsWith(QLatin1Char('<')))
                continue;
            outPieces.push_back(piece.toStdString());
        }
        if (outPieces.empty())
            return {};

        std::string decoded;
        impl.target.Decode(outPieces, &decoded);
        return QString::fromStdString(decoded).trimmed();
    } catch (const std::exception&) {
        return {};
    }
}

QStringList MarianTranslator::translate(const QStringList& lines, const QString& /*source*/,
                                        const QString& /*target*/)
{
    QStringList out;
    out.reserve(lines.size());
    for (const QString& line : lines) {
        if (line.trimmed().isEmpty()) {
            out.append(QString());
            continue;
        }
        out.append(translateLine(line));
    }
    return out;
}

} // namespace EZTranslator
