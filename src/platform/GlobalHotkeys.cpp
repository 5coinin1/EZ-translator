#include "platform/GlobalHotkeys.h"

#include <QCoreApplication>
#include <QDebug>

#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {

#ifdef _WIN32

struct ParsedHotkey
{
    UINT modifiers{0};
    UINT virtualKey{0};
    bool valid{false};
};

/** Qt::Key → virtual-key code của Win32 (các phím thường dùng cho hotkey). */
UINT virtualKeyFor(Qt::Key key)
{
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24)
        return UINT(VK_F1 + (key - Qt::Key_F1));
    if (key >= Qt::Key_A && key <= Qt::Key_Z)
        return UINT('A' + (key - Qt::Key_A));
    if (key >= Qt::Key_0 && key <= Qt::Key_9)
        return UINT('0' + (key - Qt::Key_0));

    switch (key) {
    case Qt::Key_Comma:        return VK_OEM_COMMA;
    case Qt::Key_Period:       return VK_OEM_PERIOD;
    case Qt::Key_Semicolon:    return VK_OEM_1;
    case Qt::Key_Slash:        return VK_OEM_2;
    case Qt::Key_QuoteLeft:    return VK_OEM_3;
    case Qt::Key_BracketLeft:  return VK_OEM_4;
    case Qt::Key_Backslash:    return VK_OEM_5;
    case Qt::Key_BracketRight: return VK_OEM_6;
    case Qt::Key_Apostrophe:   return VK_OEM_7;
    case Qt::Key_Minus:        return VK_OEM_MINUS;
    case Qt::Key_Equal:        return VK_OEM_PLUS;
    case Qt::Key_Space:        return VK_SPACE;
    case Qt::Key_Tab:          return VK_TAB;
    case Qt::Key_Backspace:    return VK_BACK;
    case Qt::Key_Escape:       return VK_ESCAPE;
    case Qt::Key_Insert:       return VK_INSERT;
    case Qt::Key_Delete:       return VK_DELETE;
    case Qt::Key_Home:         return VK_HOME;
    case Qt::Key_End:          return VK_END;
    case Qt::Key_PageUp:       return VK_PRIOR;
    case Qt::Key_PageDown:     return VK_NEXT;
    case Qt::Key_Up:           return VK_UP;
    case Qt::Key_Down:         return VK_DOWN;
    case Qt::Key_Left:         return VK_LEFT;
    case Qt::Key_Right:        return VK_RIGHT;
    default:                   return 0;
    }
}

ParsedHotkey parseSequence(const QKeySequence& sequence)
{
    ParsedHotkey parsed;
    if (sequence.isEmpty())
        return parsed;

    const QKeyCombination combination = sequence[0];
    const Qt::KeyboardModifiers modifiers = combination.keyboardModifiers();
    const UINT virtualKey = virtualKeyFor(combination.key());
    if (virtualKey == 0)
        return parsed;

    if (modifiers & Qt::ControlModifier)
        parsed.modifiers |= MOD_CONTROL;
    if (modifiers & Qt::AltModifier)
        parsed.modifiers |= MOD_ALT;
    if (modifiers & Qt::ShiftModifier)
        parsed.modifiers |= MOD_SHIFT;
    if (modifiers & Qt::MetaModifier)
        parsed.modifiers |= MOD_WIN;

    parsed.virtualKey = virtualKey;
    parsed.valid = true;
    return parsed;
}

#endif // _WIN32

} // namespace

GlobalHotkeys::GlobalHotkeys(QObject* parent)
    : QObject(parent)
{
    if (QCoreApplication* app = QCoreApplication::instance())
        app->installNativeEventFilter(this);
}

GlobalHotkeys::~GlobalHotkeys()
{
    clear();
    if (QCoreApplication* app = QCoreApplication::instance())
        app->removeNativeEventFilter(this);
}

bool GlobalHotkeys::isSupported()
{
#ifdef _WIN32
    return true;
#else
    return false;
#endif
}

void GlobalHotkeys::clear()
{
#ifdef _WIN32
    for (auto it = m_idByHotkeyId.cbegin(); it != m_idByHotkeyId.cend(); ++it) {
        // HWND không lưu: UnregisterHotKey(NULL, id) chỉ huỷ theo thread hiện tại —
        // đủ dùng vì apply() luôn được gọi trên GUI thread.
        UnregisterHotKey(nullptr, it.key());
    }
#endif
    m_idByHotkeyId.clear();
}

QStringList GlobalHotkeys::apply(const QList<Binding>& bindings, quintptr ownerWindow)
{
    clear();

    QStringList failed;
#ifdef _WIN32
    HWND hwnd = reinterpret_cast<HWND>(ownerWindow);
    if (!hwnd || !IsWindow(hwnd)) {
        for (const Binding& binding : bindings)
            failed.append(binding.id);
        return failed;
    }

    for (int index = 0; index < bindings.size(); ++index) {
        const Binding& binding = bindings.at(index);
        if (binding.sequence.isEmpty())
            continue; // người dùng chưa gán / đã xoá: không lỗi

        const ParsedHotkey parsed = parseSequence(binding.sequence);
        if (!parsed.valid) {
            failed.append(binding.id);
            continue;
        }

        const int hotkeyId = index + 1; // 1..N — 0 dành cho WM_APP
        if (RegisterHotKey(hwnd, hotkeyId, parsed.modifiers | MOD_NOREPEAT, parsed.virtualKey)) {
            m_idByHotkeyId.insert(hotkeyId, binding.id);
        } else {
            failed.append(binding.id);
        }
    }
#else
    for (const Binding& binding : bindings)
        failed.append(binding.id);
#endif
    return failed;
}

bool GlobalHotkeys::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* /*result*/)
{
#ifdef _WIN32
    if (eventType != QByteArrayLiteral("windows_generic_MSG"))
        return false;

    const auto* msg = static_cast<MSG*>(message);
    if (msg->message == WM_HOTKEY) {
        const QString id = m_idByHotkeyId.value(int(msg->wParam));
        if (!id.isEmpty()) {
            emit activated(id);
            return true;
        }
    }
#else
    Q_UNUSED(eventType);
    Q_UNUSED(message);
#endif
    return false;
}
