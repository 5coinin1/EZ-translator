#pragma once

#include <QPoint>
#include <QString>
#include <QWidget>

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

private:
    void placeNear(const QPoint& cursorScreenPos);

    QLabel* m_source{nullptr};
    QLabel* m_body{nullptr};
};

} // namespace EZTranslator
