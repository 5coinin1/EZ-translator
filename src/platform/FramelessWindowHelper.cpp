#include "platform/FramelessWindowHelper.h"

#include <QWidget>
#include <QWindow>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windowsx.h>
#endif

namespace EZTranslator::FramelessWindow {

#ifdef _WIN32

namespace {

/** Bán kính vùng bắt góc, tính bằng DIP (rộng hơn viền để dễ trúng góc). */
constexpr int kCornerGrabDip = 14;

int dipToPixels(const QWidget* window, int dip)
{
    const qreal ratio = window->windowHandle() ? window->windowHandle()->devicePixelRatio()
                                               : window->devicePixelRatioF();
    return qMax(1, qRound(dip * ratio));
}

} // namespace

#endif // _WIN32

bool handleNativeMessage(QWidget* window, const QByteArray& eventType, void* message,
                         qintptr* result, int resizeBorderDip)
{
#ifdef _WIN32
    if (!window || !result || eventType != QByteArrayLiteral("windows_generic_MSG"))
        return false;

    auto* msg = static_cast<MSG*>(message);
    if (!msg || msg->message != WM_NCHITTEST)
        return false;

    RECT frame{};
    if (!GetWindowRect(msg->hwnd, &frame))
        return false;

    const int width = frame.right - frame.left;
    const int height = frame.bottom - frame.top;
    if (width <= 0 || height <= 0)
        return false;

    // Cửa sổ đang maximize: không có viền để kéo.
    if (window->isMaximized())
        return false;

    const int x = GET_X_LPARAM(msg->lParam) - frame.left;
    const int y = GET_Y_LPARAM(msg->lParam) - frame.top;

    const int border = qMin(dipToPixels(window, resizeBorderDip), qMin(width, height) / 3);
    const int corner = qMin(dipToPixels(window, kCornerGrabDip), qMin(width, height) / 2);

    // Góc ưu tiên trước để bắt được cả hai chiều.
    if (x < corner && y < corner) {
        *result = HTTOPLEFT;
        return true;
    }
    if (x >= width - corner && y < corner) {
        *result = HTTOPRIGHT;
        return true;
    }
    if (x < corner && y >= height - corner) {
        *result = HTBOTTOMLEFT;
        return true;
    }
    if (x >= width - corner && y >= height - corner) {
        *result = HTBOTTOMRIGHT;
        return true;
    }

    if (x < border) {
        *result = HTLEFT;
        return true;
    }
    if (x >= width - border) {
        *result = HTRIGHT;
        return true;
    }
    if (y < border) {
        *result = HTTOP;
        return true;
    }
    if (y >= height - border) {
        *result = HTBOTTOM;
        return true;
    }

    // Phần còn lại để Qt xử lý như vùng client bình thường.
    return false;
#else
    Q_UNUSED(window);
    Q_UNUSED(eventType);
    Q_UNUSED(message);
    Q_UNUSED(result);
    Q_UNUSED(resizeBorderDip);
    return false;
#endif
}

} // namespace EZTranslator::FramelessWindow
