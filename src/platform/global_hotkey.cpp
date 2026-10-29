#include "global_hotkey.h"
#include <QCoreApplication>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
namespace clipsapp {
GlobalHotkey::GlobalHotkey(QObject* parent, int id) : QObject(parent), m_id(id) { QCoreApplication::instance()->installNativeEventFilter(this); }
GlobalHotkey::~GlobalHotkey() { unbind(); QCoreApplication::instance()->removeNativeEventFilter(this); }
void GlobalHotkey::unbind() {
#ifdef Q_OS_WIN
    if (m_bound) UnregisterHotKey(nullptr, m_id);
#endif
    m_bound = false;
}
unsigned windowsVirtualKey(QKeyCombination combination) {
#ifdef Q_OS_WIN
    const int key = combination.key();
    // The keypad has keys of its own, whatever the layout, that Qt marks with a modifier.
    if (combination.keyboardModifiers() & Qt::KeypadModifier) {
        if (key >= Qt::Key_0 && key <= Qt::Key_9) return VK_NUMPAD0 + unsigned(key - Qt::Key_0);
        switch (key) {
        case Qt::Key_Plus: return VK_ADD;
        case Qt::Key_Minus: return VK_SUBTRACT;
        case Qt::Key_Asterisk: return VK_MULTIPLY;
        case Qt::Key_Slash: return VK_DIVIDE;
        case Qt::Key_Period: case Qt::Key_Comma: return VK_DECIMAL;
        default: break;
        }
    }
    if ((key >= Qt::Key_A && key <= Qt::Key_Z) || (key >= Qt::Key_0 && key <= Qt::Key_9)) return unsigned(key);
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24) return VK_F1 + unsigned(key - Qt::Key_F1);
    switch (key) {
    case Qt::Key_Space: return VK_SPACE;
    case Qt::Key_Insert: return VK_INSERT;
    case Qt::Key_Delete: return VK_DELETE;
    case Qt::Key_Home: return VK_HOME;
    case Qt::Key_End: return VK_END;
    case Qt::Key_PageUp: return VK_PRIOR;
    case Qt::Key_PageDown: return VK_NEXT;
    case Qt::Key_Left: return VK_LEFT;
    case Qt::Key_Right: return VK_RIGHT;
    case Qt::Key_Up: return VK_UP;
    case Qt::Key_Down: return VK_DOWN;
    case Qt::Key_Return: case Qt::Key_Enter: return VK_RETURN;
    case Qt::Key_Backspace: return VK_BACK;
    case Qt::Key_Tab: return VK_TAB;
    case Qt::Key_Escape: return VK_ESCAPE;
    case Qt::Key_Pause: return VK_PAUSE;
    case Qt::Key_Print: return VK_SNAPSHOT;
    case Qt::Key_ScrollLock: return VK_SCROLL;
    case Qt::Key_MediaPlay: case Qt::Key_MediaPause: case Qt::Key_MediaTogglePlayPause: return VK_MEDIA_PLAY_PAUSE;
    case Qt::Key_MediaStop: return VK_MEDIA_STOP;
    case Qt::Key_MediaNext: return VK_MEDIA_NEXT_TRACK;
    case Qt::Key_MediaPrevious: return VK_MEDIA_PREV_TRACK;
    default: break;
    }
    // Any other character is the key that writes it on the keyboard in use: « é », « ) » or « ^ » of
    // AZERTY, « ; » or « = » of QWERTY. The shift it may need is the shortcut's own modifier.
    if (key > 0x20 && key < 0x10000) {
        const SHORT found = VkKeyScanW(wchar_t(QChar(key).toLower().unicode()));
        if (found != -1 && LOBYTE(found) != 0xFF) return LOBYTE(found);
    }
#else
    Q_UNUSED(combination);
#endif
    return 0;
}
bool GlobalHotkey::bind(const QKeySequence& sequence, QString& error) {
    unbind(); if (sequence.isEmpty()) return true;
#ifdef Q_OS_WIN
    const auto combination = sequence[0]; const auto modifiers = combination.keyboardModifiers();
    const UINT nativeKey = windowsVirtualKey(combination);
    if (!nativeKey) { error = QStringLiteral("Ce raccourci n'est pas pris en charge. Choisissez par exemple Ctrl+Alt+F10."); return false; }
    UINT flags = MOD_NOREPEAT;
    if (modifiers & Qt::ControlModifier) flags |= MOD_CONTROL;
    if (modifiers & Qt::AltModifier) flags |= MOD_ALT;
    if (modifiers & Qt::ShiftModifier) flags |= MOD_SHIFT;
    if (modifiers & Qt::MetaModifier) flags |= MOD_WIN;
    if (!RegisterHotKey(nullptr, m_id, flags, nativeKey)) { error = QStringLiteral("Le raccourci est déjà utilisé ou réservé par Windows. Choisissez-en un autre."); return false; }
    m_bound = true; return true;
#else
    error = QStringLiteral("Les raccourcis globaux Linux seront intégrés avec le portail KDE."); return false;
#endif
}
bool GlobalHotkey::nativeEventFilter(const QByteArray&, void* message, qintptr* result) {
#ifdef Q_OS_WIN
    const auto* event = static_cast<MSG*>(message);
    if (m_bound && event->message == WM_HOTKEY && event->wParam == WPARAM(m_id)) { if (result) *result = 0; emit activated(); return true; }
#else
    Q_UNUSED(message); Q_UNUSED(result);
#endif
    return false;
}
}
