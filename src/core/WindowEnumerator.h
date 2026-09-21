#pragma once

#include "core/Types.h"
#include <QList>

namespace EZTranslator {

/**
 * @brief Tiện ích quét danh sách các cửa sổ ứng dụng đang mở trên hệ điều hành (Windows).
 */
class WindowEnumerator
{
public:
    /**
     * @brief Quét và trả về danh sách các cửa sổ đang hiển thị trên desktop.
     * Tự động lọc bỏ các cửa sổ hệ thống ngầm, desktop, taskbar, và cửa sổ của chính EZ-Translator.
     */
    static QList<WindowInfo> enumerateWindows();

    /**
     * @brief Kiểm tra một window handle có còn tồn tại và hiển thị không.
     */
    static bool isWindowValid(quintptr handle);
};

} // namespace EZTranslator
