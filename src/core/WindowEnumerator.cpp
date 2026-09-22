#include "WindowEnumerator.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dwmapi.h>
#include <psapi.h>
#include <shellapi.h>

#include <QImage>
#include <QPixmap>
#include <QFileInfo>
#include <string>

namespace {

struct EnumData {
    QList<EZTranslator::WindowInfo> windows;
    DWORD currentProcessId{0};
};

bool isCandidateWindow(HWND hwnd, DWORD currentPid)
{
    if (!IsWindow(hwnd) || !IsWindowVisible(hwnd)) {
        return false;
    }

    // Không chọn chính cửa sổ của EZ-Translator
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0 || pid == currentPid) {
        return false;
    }

    // Bỏ qua cửa sổ có kích thước quá nhỏ (không phải app thực thụ)
    RECT rc;
    if (!GetWindowRect(hwnd, &rc)) {
        return false;
    }
    if ((rc.right - rc.left) <= 30 || (rc.bottom - rc.top) <= 30) {
        return false;
    }

    // Bỏ qua cửa sổ không có tiêu đề
    int length = GetWindowTextLengthW(hwnd);
    if (length <= 0) {
        return false;
    }

    // Chỉ lấy cửa sổ cấp cao nhất thực sự (không phải popup/window con có owner)
    if (GetWindow(hwnd, GW_OWNER) != nullptr) {
        return false;
    }

    // Lọc theo Extended Style: bỏ ToolWindow trừ khi có AppWindow
    LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if ((exStyle & WS_EX_TOOLWINDOW) && !(exStyle & WS_EX_APPWINDOW)) {
        return false;
    }

    // Lọc DWM Cloaked (Windows 10/11: app ngầm, desktop ảo khác, v.v.)
    int cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked)))) {
        if (cloaked != 0) {
            return false;
        }
    }

    // Lọc theo Window Class hệ thống
    wchar_t className[256];
    if (GetClassNameW(hwnd, className, sizeof(className) / sizeof(wchar_t)) > 0) {
        std::wstring cls = className;
        if (cls == L"Progman" || cls == L"WorkerW" ||
            cls == L"Shell_TrayWnd" || cls == L"Shell_SecondaryTrayWnd" ||
            cls == L"Windows.UI.Core.CoreWindow" ||
            cls == L"ForegroundStaging" ||
            cls == L"Sidekick_SidekickContainer") {
            return false;
        }
    }

    return true;
}

void getProcessDetails(HWND hwnd, QString& processName, QString& exePath)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) return;

    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (hProc) {
        wchar_t buf[MAX_PATH];
        DWORD size = MAX_PATH;
        if (QueryFullProcessImageNameW(hProc, 0, buf, &size)) {
            exePath = QString::fromWCharArray(buf);
            processName = QFileInfo(exePath).fileName();
        }
        CloseHandle(hProc);
    }
}

QIcon extractWindowIcon(HWND hwnd, const QString& exePath)
{
    HICON hIcon = nullptr;

    // 1. Thử gửi tin nhắn WM_GETICON
    SendMessageTimeoutW(hwnd, WM_GETICON, ICON_SMALL2, 0,
                        SMTO_ABORTIFHUNG | SMTO_BLOCK, 80, reinterpret_cast<PDWORD_PTR>(&hIcon));
    if (!hIcon) {
        SendMessageTimeoutW(hwnd, WM_GETICON, ICON_SMALL, 0,
                            SMTO_ABORTIFHUNG | SMTO_BLOCK, 80, reinterpret_cast<PDWORD_PTR>(&hIcon));
    }
    if (!hIcon) {
        SendMessageTimeoutW(hwnd, WM_GETICON, ICON_BIG, 0,
                            SMTO_ABORTIFHUNG | SMTO_BLOCK, 80, reinterpret_cast<PDWORD_PTR>(&hIcon));
    }

    // 2. Thử lấy icon từ Window Class
    if (!hIcon) {
        hIcon = reinterpret_cast<HICON>(GetClassLongPtrW(hwnd, GCLP_HICONSM));
    }
    if (!hIcon) {
        hIcon = reinterpret_cast<HICON>(GetClassLongPtrW(hwnd, GCLP_HICON));
    }

    // 3. Nếu chưa có icon, trích xuất icon từ file exe của tiến trình
    bool needsDestroy = false;
    if (!hIcon && !exePath.isEmpty()) {
        std::wstring wPath = exePath.toStdWString();
        SHFILEINFOW sfi = {};
        if (SHGetFileInfoW(wPath.c_str(), 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_SMALLICON)) {
            hIcon = sfi.hIcon;
            needsDestroy = true;
        }
    }

    if (hIcon) {
        QImage img = QImage::fromHICON(hIcon);
        if (needsDestroy) {
            DestroyIcon(hIcon);
        }
        if (!img.isNull()) {
            return QIcon(QPixmap::fromImage(img));
        }
    }

    return QIcon();
}

BOOL CALLBACK EnumWindowsCallback(HWND hwnd, LPARAM lParam)
{
    auto* data = reinterpret_cast<EnumData*>(lParam);
    if (!isCandidateWindow(hwnd, data->currentProcessId)) {
        return TRUE;
    }

    wchar_t titleBuf[512];
    int len = GetWindowTextW(hwnd, titleBuf, sizeof(titleBuf) / sizeof(wchar_t));
    if (len <= 0) {
        return TRUE;
    }
    QString title = QString::fromWCharArray(titleBuf).trimmed();
    if (title.isEmpty() || title == "Program Manager") {
        return TRUE;
    }

    QString processName;
    QString exePath;
    getProcessDetails(hwnd, processName, exePath);

    QIcon icon = extractWindowIcon(hwnd, exePath);

    EZTranslator::WindowInfo info;
    info.handle = reinterpret_cast<quintptr>(hwnd);
    info.title = title;
    info.processName = processName;
    info.icon = icon;
    info.isVisible = true;

    data->windows.append(info);
    return TRUE;
}

} // anonymous namespace

namespace EZTranslator {

QList<WindowInfo> WindowEnumerator::enumerateWindows()
{
    EnumData data;
    data.currentProcessId = GetCurrentProcessId();

    HDESK hDesk = OpenDesktopA("default", 0, FALSE, DESKTOP_ENUMERATE | DESKTOP_READOBJECTS | GENERIC_ALL);
    if (hDesk) {
        EnumDesktopWindows(hDesk, EnumWindowsCallback, reinterpret_cast<LPARAM>(&data));
        CloseDesktop(hDesk);
    } else {
        EnumWindows(EnumWindowsCallback, reinterpret_cast<LPARAM>(&data));
    }

    return data.windows;
}

bool WindowEnumerator::isWindowValid(quintptr handle)
{
    HWND hwnd = reinterpret_cast<HWND>(handle);
    return IsWindow(hwnd) && IsWindowVisible(hwnd);
}

} // namespace EZTranslator

#else // Non-Windows fallback

namespace EZTranslator {

QList<WindowInfo> WindowEnumerator::enumerateWindows()
{
    return {};
}

bool WindowEnumerator::isWindowValid(quintptr /*handle*/)
{
    return false;
}

} // namespace EZTranslator

#endif
