#pragma once

#include "translation/ITranslator.h"

#include <memory>

namespace EZTranslator {

/**
 * @brief Engine dịch online qua Google Translate (endpoint công khai `gtx`).
 *
 * Không cần model, chạy được ngay; đổi lại cần mạng. Dùng làm engine mặc định.
 * Gọi đồng bộ (đợi mạng) nên chỉ nên gọi cho các unit MỚI chưa có trong cache.
 */
class GoogleTranslator final : public ITranslator
{
public:
    GoogleTranslator();
    ~GoogleTranslator() override;

    GoogleTranslator(const GoogleTranslator&) = delete;
    GoogleTranslator& operator=(const GoogleTranslator&) = delete;

    [[nodiscard]] bool isReady() const override { return true; }
    [[nodiscard]] QString name() const override { return QStringLiteral("Google"); }

    [[nodiscard]] QStringList translate(const QStringList& lines, const QString& source,
                                        const QString& target) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace EZTranslator
