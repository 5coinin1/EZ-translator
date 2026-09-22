#pragma once
#include <QWidget>
#include <QString>

/** Loại icon cho SectionTitle */
enum class SectionIconType {
    Gamepad,     ///< Icon tay cầm game (Section 1)
    Translate,   ///< Icon dịch văn bản 文A (Section 2)
    Monitor,     ///< Icon màn hình máy tính (Section 3: Hiển thị bản dịch)
    Crop,        ///< Icon chọn vùng dịch (Section 4: Chọn vùng dịch)
    Profile,     ///< Icon hồ sơ / tài liệu
};

/** Tiêu đề phần có icon vector custom và số thứ tự */
class SectionTitle : public QWidget
{
    Q_OBJECT
public:
    explicit SectionTitle(SectionIconType iconType,
                          const QString& text,
                          QWidget* parent = nullptr);

    // Overload giữ tương thích cũ (dùng emoji fallback)
    explicit SectionTitle(const QString& icon,
                          const QString& text,
                          QWidget* parent = nullptr);
};
