#include "ocr/OcrRuntime.h"

#include <QList>
#include <QStringLiteral>

#include <stdexcept>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// Header ONNX Runtime dùng `_stdcall` (chỉ MSVC định nghĩa); MinGW chỉ có
// `__stdcall`. Shim để build được ONNX Runtime (bản DirectML chính thức là MSVC)
// bằng MinGW — ABI trên x64 vốn không phân biệt calling convention.
#if defined(__MINGW32__) && !defined(_stdcall)
#define _stdcall __stdcall
#endif
// SAL annotation chỉ MSVC có; ORT tự fallback nhưng MinGW sal.h định nghĩa trước.
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
    QString preference{QStringLiteral("auto")};
    int adapterIndex{0};
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

QString OcrRuntime::configure(const QString& devicePreference, int adapterIndex, QString* error)
{
    if (!m_impl)
        return {};

    QString preference = devicePreference.trimmed().toLower();
    if (preference != QLatin1String("cpu") && preference != QLatin1String("directml")
        && preference != QLatin1String("cuda") && preference != QLatin1String("auto")) {
        preference = QStringLiteral("auto");
    }
    m_impl->preference = preference;
    m_impl->adapterIndex = qMax(0, adapterIndex);
    m_impl->provider = QStringLiteral("CPU");
#if defined(_WIN32)
    if (preference != QLatin1String("cpu")) {
        // Đặt provider ưu tiên TRƯỚC khi tạo session để OcrRecognizer chọn đúng
        // model fp16/int8 theo GPU. Nếu session lỗi, createSession sẽ fallback.
        if (preference != QLatin1String("directml")) {
            if (findProviderSymbol("OrtSessionOptionsAppendExecutionProvider_CUDA"))
                m_impl->provider = QStringLiteral("CUDA");
        }
        if (m_impl->provider == QLatin1String("CPU")
            && (preference == QLatin1String("auto")
                || preference == QLatin1String("directml")
                || preference == QLatin1String("cuda"))) {
            if (findProviderSymbol("OrtSessionOptionsAppendExecutionProvider_DML"))
                m_impl->provider = QStringLiteral("DirectML");
        }
    }
#endif
    if (error)
        error->clear();
    return m_impl->provider;
}

std::shared_ptr<OcrSession> OcrRuntime::createSession(const QString& modelPath, int intraThreads,
                                                      int interThreads, bool sequential,
                                                      QString* error)
{
    if (!m_impl)
        return nullptr;

    struct Candidate
    {
        const char* symbol;
        const char* name;
    };
    QList<Candidate> candidates;
    if (m_impl->preference == QLatin1String("directml")) {
        candidates.append(Candidate{"OrtSessionOptionsAppendExecutionProvider_DML", "DirectML"});
    } else if (m_impl->preference != QLatin1String("cpu")) {
        // auto/cuda: CUDA trước, DirectML là fallback kế tiếp.
        candidates.append(Candidate{"OrtSessionOptionsAppendExecutionProvider_CUDA", "CUDA"});
        candidates.append(Candidate{"OrtSessionOptionsAppendExecutionProvider_DML", "DirectML"});
    }

    const auto buildOptions = [&](void* providerSymbol) {
        Ort::SessionOptions options;
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        if (intraThreads > 0)
            options.SetIntraOpNumThreads(intraThreads);
        if (interThreads > 0)
            options.SetInterOpNumThreads(interThreads);
        if (providerSymbol) {
            // GPU (DML/CUDA) thực thi kernel tuần tự; song song chỉ gây tranh chấp.
            options.SetExecutionMode(sequential ? ExecutionMode::ORT_SEQUENTIAL
                                                : ExecutionMode::ORT_PARALLEL);
            auto append = reinterpret_cast<AppendProviderFn>(providerSymbol);
            if (OrtStatus* status = append(options, m_impl->adapterIndex)) {
                const char* message = Ort::GetApi().GetErrorMessage(status);
                const QString detail = QString::fromUtf8(message ? message : "append failed");
                Ort::GetApi().ReleaseStatus(status);
                throw std::runtime_error(detail.toStdString());
            }
        }
        return options;
    };

    const auto makeSession = [&](void* providerSymbol) -> std::shared_ptr<OcrSession> {
        auto session = std::make_unique<Ort::Session>(
            m_impl->env, modelPath.toStdWString().c_str(), buildOptions(providerSymbol));
        return std::make_shared<OrtSessionAdapter>(std::move(session));
    };

    QString lastGpuError;
#if defined(_WIN32)
    for (const Candidate& candidate : candidates) {
        void* symbol = findProviderSymbol(candidate.symbol);
        if (!symbol)
            continue;
        try {
            if (auto session = makeSession(symbol)) {
                m_impl->provider = QString::fromLatin1(candidate.name);
                if (error)
                    error->clear();
                return session;
            }
        } catch (const std::exception& exception) {
            lastGpuError = QString::fromUtf8(exception.what());
        }
    }
#endif

    try {
        if (auto session = makeSession(nullptr)) {
            m_impl->provider = QStringLiteral("CPU");
            if (error) {
                // Giữ lý do GPU lỗi để tầng trên hiển thị fallback; không coi là
                // lỗi load vì CPU session đã thành công.
                *error = lastGpuError;
            }
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
