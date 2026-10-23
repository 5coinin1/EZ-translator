#pragma once

#include <QString>
#include <QtGlobal>

namespace EZTranslator::Platform {

/**
 * @brief Bật/tắt việc cửa sổ bị loại khỏi capture (Windows: SetWindowDisplayAffinity).
 *
 * Dùng cho lớp overlay/ popup: bật thì chữ dịch vẫn hiện trên màn hình nhưng
 * không lọt vào ảnh chụp/quay màn hình (và cả chính pipeline capture của app).
 * Trả false nếu OS không hỗ trợ hoặc handle không hợp lệ.
 */
bool setExcludedFromCapture(quintptr windowHandle, bool excluded);

/**
 * @brief Bật/tắt khởi động cùng Windows cho một đường dẫn exe cụ thể.
 *
 * Ghi/xoá giá trị trong HKCU\Software\Microsoft\Windows\CurrentVersion\Run.
 * Trả false nếu không ghi được (registry bị chặn).
 */
bool setAutoStart(const QString& appName, const QString& executablePath, bool enabled);

/** Kiểm tra app có đang được bật khởi động cùng Windows không. */
bool isAutoStartEnabled(const QString& appName);

} // namespace EZTranslator::Platform
