#pragma once

#include <QColor>
#include <QPoint>
#include <QString>
#include <QWidget>

#include "core/Types.h"

class QLabel;

namespace EZTranslator {

/**
 * @brief Cửa sổ nổi nhỏ cạnh con trỏ hiển thị bản dịch của DÒNG đang trỏ tới.
 *
 * Dùng cho chế độ "dịch khi chỉ chuột". Không viền, luôn trên cùng, **không nhận
 * focus** và **không nhận chuột** nên ứng dụng gốc vẫn hoạt động bình thường.
 */
class TranslationPopup : public QWidget
{
    Q_OBJECT
public:
    explicit TranslationPopup(QWidget* parent = nullptr);

    /** Hiện `translation` (kèm dòng gốc nếu khác) cạnh con trỏ.
     *
     *  @param sourceWidthLogical bề rộng box nguồn (logical px) để không cho popup
     *                            tràn sang vùng UI bên cạnh; 0 = dùng mặc định.
     */
    void showTranslation(const QString& source, const QString& translation,
                         const QPoint& cursorScreenPos, int sourceWidthLogical = 0);

    void hidePopup();

    /** Áp dụng font/màu/kiểu từ Cài đặt → Giao diện cho popup hover. */
    void setAppearance(const EZTranslator::AppSettings& settings);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void applyAppearance();
    void placeNear(const QPoint& cursorScreenPos);

    QLabel* m_source{nullptr};
    QLabel* m_body{nullptr};
    EZTranslator::AppSettings m_appearance;
    QColor m_popupBackground{QColor(16, 18, 24, 238)};
    QColor m_popupBorder{QColor(130, 150, 180, 170)};
    int    m_cornerRadius{9};
    bool   m_drawBorder{true};
};

} // namespace EZTranslator
