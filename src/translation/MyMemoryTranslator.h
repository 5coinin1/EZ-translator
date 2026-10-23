#pragma once

#include "translation/ITranslator.h"

#include <memory>

namespace EZTranslator {

/**
 * @brief Engine dịch online dự phòng qua MyMemory (api.mymemory.translated.net).
 *
 * Dùng khi engine chính (Google) bị chặn/giới hạn. Chất lượng thấp hơn nhưng
 * độc lập về hạ tầng nên giúp pipeline vẫn có bản dịch.
 */
class MyMemoryTranslator final : public ITranslator
{
public:
    MyMemoryTranslator();
    ~MyMemoryTranslator() override;

    MyMemoryTranslator(const MyMemoryTranslator&) = delete;
    MyMemoryTranslator& operator=(const MyMemoryTranslator&) = delete;

    [[nodiscard]] bool isReady() const override { return true; }
    [[nodiscard]] QString name() const override { return QStringLiteral("MyMemory"); }

    [[nodiscard]] QStringList translate(const QStringList& lines, const QString& source,
                                        const QString& target) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace EZTranslator
