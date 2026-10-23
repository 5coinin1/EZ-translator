#pragma once

#include "translation/ITranslator.h"

#include <memory>

namespace EZTranslator {

/**
 * @brief Engine dịch online qua Google Translate.
 *
 * Có 2 chế độ:
 *  - Có API key → dùng Cloud Translation API chính thức (v2), batch nhiều câu
 *    trong một request, hạn mức/thanh toán theo tài khoản Google Cloud.
 *  - Không có key → dùng endpoint công khai `translate_a/single` (`client=gtx`),
 *    không cần cấu hình nhưng có thể bị giới hạn.
 *
 * Gọi đồng bộ (đợi mạng) nên chỉ nên gọi cho các unit MỚI chưa có trong cache.
 */
class GoogleTranslator final : public ITranslator
{
public:
    GoogleTranslator();
    ~GoogleTranslator() override;

    GoogleTranslator(const GoogleTranslator&) = delete;
    GoogleTranslator& operator=(const GoogleTranslator&) = delete;

    /** API key của Google Cloud Translation; rỗng → endpoint miễn phí. */
    void setApiKey(const QString& apiKey);

    [[nodiscard]] bool isReady() const override { return true; }
    [[nodiscard]] QString name() const override { return QStringLiteral("Google"); }

    [[nodiscard]] QStringList translate(const QStringList& lines, const QString& source,
                                        const QString& target) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace EZTranslator
