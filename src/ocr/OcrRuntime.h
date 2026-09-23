#pragma once

#include <QtGlobal>

#include <functional>
#include <memory>
#include <vector>

#include <QString>

namespace EZTranslator {

/**
 * @brief Một session ONNX đã nạp, trừu tượng hoá để Detector/Recognizer không
 *        phải phụ thuộc trực tiếp vào ONNX Runtime.
 *
 * `run()` nhận con trỏ input (không copy) và gọi callback với output tensor đầu
 * tiên (cũng không copy) — quan trọng vì output logits của rec có thể rất lớn.
 */
class OcrSession
{
public:
    using OutputCallback = std::function<void(const float* data,
                                              const std::vector<int64_t>& shape)>;

    virtual ~OcrSession() = default;

    /** Chạy 1 forward pass. Trả false nếu lỗi. */
    virtual bool run(const float* input, const std::vector<int64_t>& inputShape,
                     const OutputCallback& onOutput) = 0;

    /** True nếu batch dimension của input là động (cho phép gộp nhiều dòng). */
    [[nodiscard]] virtual bool dynamicBatch() const = 0;
};

/**
 * @brief Sở hữu ONNX Runtime Env + chọn Execution Provider, tạo session cho model.
 *
 * Chỉ .cpp của lớp này include ONNX Runtime, nên phần còn lại của module OCR
 * không bị lệ thuộc header ORT.
 */
class OcrRuntime
{
public:
    OcrRuntime();
    ~OcrRuntime();

    OcrRuntime(const OcrRuntime&) = delete;
    OcrRuntime& operator=(const OcrRuntime&) = delete;

    /**
     * @brief Chọn Execution Provider.
     * @param useGpu  true → thử DirectML/CUDA, thất bại thì rơi về CPU.
     * @return Tên provider thực tế ("DirectML"/"CUDA"/"CPU").
     */
    [[nodiscard]] QString configure(bool useGpu, QString* error = nullptr);

    [[nodiscard]] QString providerName() const;

    /**
     * @brief Tạo session cho model.
     * @param sequential  true → chạy kernel tuần tự (đúng cho GPU).
     * @return nullptr + @p error nếu nạp thất bại.
     */
    [[nodiscard]] std::shared_ptr<OcrSession> createSession(const QString& modelPath,
                                                            int threads, bool sequential,
                                                            QString* error = nullptr);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace EZTranslator
