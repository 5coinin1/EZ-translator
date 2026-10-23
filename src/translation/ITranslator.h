#pragma once

#include <QString>
#include <QStringList>

namespace EZTranslator {

/**
 * @brief Interface cho engine dịch.
 *
 * Đơn vị dịch là một "unit" (câu/đoạn) đã được ghép từ OCR. Implementation có
 * thể offline (model local) hoặc online (Google). Tách interface để có thể thay
 * engine mà không đụng tới pipeline.
 */
class ITranslator
{
public:
    virtual ~ITranslator() = default;

    /** Engine đã sẵn sàng dùng chưa. */
    [[nodiscard]] virtual bool isReady() const = 0;

    /** Tên engine (dùng cho log/cấu hình). */
    [[nodiscard]] virtual QString name() const = 0;

    /**
     * @brief Dịch nhiều dòng.
     * @return danh sách cùng số phần tử; phần tử rỗng nghĩa là dòng đó lỗi.
     */
    [[nodiscard]] virtual QStringList translate(const QStringList& lines, const QString& source,
                                                const QString& target) = 0;
};

} // namespace EZTranslator
