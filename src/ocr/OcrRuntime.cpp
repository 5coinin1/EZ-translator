#include "ocr/OcrRuntime.h"

#include <stdexcept>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#if defined(_WIN32) && !defined(_Frees_ptr_opt_)
#define _Frees_ptr_opt_
#endif

#include <onnxruntime_cxx_api.h>

namespace EZTranslator {

namespace {

/** Bọc Ort::Session thành OcrSession (không copy input/output). */
class OrtSessionAdapter final : public OcrSession
{
public:
    explicit OrtSessionAdapter(std::unique_ptr<Ort::Session> session)
        : m_session(std::move(session))
    {
        Ort::AllocatorWithDefaultOptions allocator;
        m_inputName = m_session->GetInputNameAllocated(0, allocator).get();
        m_outputName = m_session->GetOutputNameAllocated(0, allocator).get();

        const auto shape = m_session->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
        m_dynamicBatch = shape.size() == 4 && shape[0] <= 0;
    }

    bool run(const float* input, const std::vector<int64_t>& inputShape,
             const OutputCallback& onOutput) override
    {
        try {
            const Ort::MemoryInfo memoryInfo =
                Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
            Ort::Value tensor = Ort::Value::CreateTensor<float>(
                memoryInfo, const_cast<float*>(input), elementCount(inputShape), inputShape.data(),
                inputShape.size());

            const char* inputs[] = {m_inputName.c_str()};
            const char* outputs[] = {m_outputName.c_str()};
            auto result =
                m_session->Run(Ort::RunOptions{nullptr}, inputs, &tensor, 1, outputs, 1);
            if (result.empty())
                return false;

            const float* data = result[0].GetTensorMutableData<float>();
            const auto shape = result[0].GetTensorTypeAndShapeInfo().GetShape();
            onOutput(data, shape);
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }

    [[nodiscard]] bool dynamicBatch() const override { return m_dynamicBatch; }

private:
    static size_t elementCount(const std::vector<int64_t>& shape)
    {
        size_t count = 1;
        for (const int64_t dimension : shape)
            count *= size_t(dimension);
        return count;
    }

    std::unique_ptr<Ort::Session> m_session;
    std::string m_inputName;
    std::string m_outputName;
    bool m_dynamicBatch{false};
};

#if defined(_WIN32)
using AppendProviderFn = OrtStatus* (*)(OrtSessionOptions*, int);

void* findProviderSymbol(const char* symbol)
{
    HMODULE module = GetModuleHandleW(L"onnxruntime.dll");
    if (!module)
        return nullptr;
    return reinterpret_cast<void*>(GetProcAddress(module, symbol));
}
#endif

} // namespace

struct OcrRuntime::Impl
{
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "ez-translator-ocr"};
    QString provider{QStringLiteral("CPU")};
    void*   providerSymbol{nullptr};
};

OcrRuntime::OcrRuntime()
    : m_impl(std::make_unique<Impl>())
{
}

OcrRuntime::~OcrRuntime() = default;

QString OcrRuntime::providerName() const
{
    return m_impl ? m_impl->provider : QString();
}

QString OcrRuntime::configure(bool useGpu, QString* error)
{
    if (!m_impl)
        return {};

    if (!useGpu) {
        m_impl->provider = QStringLiteral("CPU");
        m_impl->providerSymbol = nullptr;
        return m_impl->provider;
    }

#if defined(_WIN32)
    // Provider được resolve lúc chạy để một binary dùng được cho cả build
    // CPU / DirectML / CUDA: thiếu symbol nghĩa là "không có GPU".
    struct Candidate
    {
        const char* symbol;
        const char* name;
    };
    static const Candidate candidates[] = {
        {"OrtSessionOptionsAppendExecutionProvider_CUDA", "CUDA"},
        {"OrtSessionOptionsAppendExecutionProvider_DML", "DirectML"},
    };
    for (const Candidate& candidate : candidates) {
        if (void* symbol = findProviderSymbol(candidate.symbol)) {
            m_impl->providerSymbol = symbol;
            m_impl->provider = QString::fromLatin1(candidate.name);
            return m_impl->provider;
        }
    }
    if (error)
        *error = QStringLiteral("ONNX Runtime build không có GPU provider");
#else
    if (error)
        *error = QStringLiteral("unsupported platform");
#endif

    m_impl->provider = QStringLiteral("CPU");
    m_impl->providerSymbol = nullptr;
    return m_impl->provider;
}

std::shared_ptr<OcrSession> OcrRuntime::createSession(const QString& modelPath, int threads,
                                                      bool sequential, QString* error)
{
    if (!m_impl)
        return nullptr;

    const auto buildOptions = [&](bool useProvider) {
        Ort::SessionOptions options;
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        if (threads > 0)
            options.SetIntraOpNumThreads(threads);
        if (useProvider) {
            // GPU (DML/CUDA) thực thi kernel tuần tự; song song chỉ gây tranh chấp.
            options.SetExecutionMode(sequential ? ExecutionMode::ORT_SEQUENTIAL
                                                : ExecutionMode::ORT_PARALLEL);
            auto append = reinterpret_cast<AppendProviderFn>(m_impl->providerSymbol);
            if (OrtStatus* status = append(options, 0)) {
                const char* message = Ort::GetApi().GetErrorMessage(status);
                const QString detail = QString::fromUtf8(message ? message : "append failed");
                Ort::GetApi().ReleaseStatus(status);
                throw std::runtime_error(detail.toStdString());
            }
        }
        return options;
    };

    const auto makeSession = [&](bool useProvider) -> std::shared_ptr<OcrSession> {
        auto session = std::make_unique<Ort::Session>(m_impl->env, modelPath.toStdWString().c_str(),
                                                      buildOptions(useProvider));
        return std::make_shared<OrtSessionAdapter>(std::move(session));
    };

    if (m_impl->providerSymbol) {
        try {
            if (auto session = makeSession(true))
                return session;
        } catch (const std::exception& exception) {
            // Provider lỗi (thiếu dependency, driver…) → tắt và rơi về CPU.
            if (error)
                *error = QString::fromUtf8(exception.what());
            m_impl->providerSymbol = nullptr;
            m_impl->provider = QStringLiteral("CPU");
        }
    }

    try {
        if (auto session = makeSession(false)) {
            if (error)
                error->clear();
            return session;
        }
    } catch (const std::exception& exception) {
        if (error)
            *error = QString::fromUtf8(exception.what());
    }

    if (error && error->isEmpty())
        *error = QStringLiteral("Không tạo được ONNX session: ") + modelPath;
    return nullptr;
}

} // namespace EZTranslator
