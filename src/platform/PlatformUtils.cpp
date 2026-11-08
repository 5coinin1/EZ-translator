#include "platform/PlatformUtils.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QString>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dwmapi.h>
#endif

namespace EZTranslator::Platform {

#ifdef _WIN32

namespace {

/// WDA_EXCLUDEFROMCAPTURE chỉ có từ Windows 10 2004; máy cũ hơn dùng WDA_MONITOR.
constexpr DWORD kExcludeFromCapture = 0x00000011;
constexpr DWORD kMonitorOnly = 0x00000001;

constexpr auto kRunKeyPath = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";

} // namespace

bool setRoundedCorners(quintptr windowHandle, bool enabled)
{
    HWND hwnd = reinterpret_cast<HWND>(windowHandle);
    if (!hwnd || !IsWindow(hwnd))
        return false;

    // DWMWA_WINDOW_CORNER_PREFERENCE = 33 (Windows 11). DWMWCP_ROUND = 2,
    // DWMWCP_DONOTROUND = 1. Trên Windows 10 API trả E_INVALIDARG -> false.
    constexpr DWORD kCornerPreferenceAttribute = 33;
    constexpr int kDoNotRound = 1;
    constexpr int kRound = 2;
    const int preference = enabled ? kRound : kDoNotRound;
    const HRESULT hr = DwmSetWindowAttribute(hwnd, kCornerPreferenceAttribute,
                                             &preference, sizeof(preference));
    return SUCCEEDED(hr);
}

bool setExcludedFromCapture(quintptr windowHandle, bool excluded)
{
    HWND hwnd = reinterpret_cast<HWND>(windowHandle);
    if (!hwnd || !IsWindow(hwnd))
        return false;

    if (!excluded)
        return SetWindowDisplayAffinity(hwnd, WDA_NONE);

    if (SetWindowDisplayAffinity(hwnd, kExcludeFromCapture))
        return true;
    // Win10 cũ: vẫn ẩn được khỏi capture kiểu "màn hình đen" (WDA_MONITOR).
    return SetWindowDisplayAffinity(hwnd, kMonitorOnly);
}

bool setAutoStart(const QString& appName, const QString& executablePath, bool enabled)
{
    if (appName.isEmpty() || executablePath.isEmpty())
        return false;

    QSettings runKey(QString::fromLatin1(kRunKeyPath), QSettings::NativeFormat);
    if (enabled) {
        const QString quoted = QStringLiteral("\"%1\"")
                                   .arg(QDir::toNativeSeparators(executablePath));
        runKey.setValue(appName, quoted);
    } else {
        runKey.remove(appName);
    }
    runKey.sync();
    return runKey.status() == QSettings::NoError;
}

bool isAutoStartEnabled(const QString& appName)
{
    if (appName.isEmpty())
        return false;
    QSettings runKey(QString::fromLatin1(kRunKeyPath), QSettings::NativeFormat);
    return runKey.contains(appName);
}

#else // Non-Windows: không hỗ trợ

bool setExcludedFromCapture(quintptr, bool) { return false; }
bool setAutoStart(const QString&, const QString&, bool) { return false; }
bool isAutoStartEnabled(const QString&) { return false; }
bool setRoundedCorners(quintptr, bool) { return false; }

#endif

} // namespace EZTranslator::Platform
