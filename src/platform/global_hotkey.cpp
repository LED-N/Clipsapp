#include "global_hotkey.h"
#include <QCoreApplication>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
namespace clipsapp {
GlobalHotkey::GlobalHotkey(QObject* parent) : QObject(parent) { QCoreApplication::instance()->installNativeEventFilter(this); }
GlobalHotkey::~GlobalHotkey() { unbind(); QCoreApplication::instance()->removeNativeEventFilter(this); }
void GlobalHotkey::unbind() {
#ifdef Q_OS_WIN
    if (m_bound) UnregisterHotKey(nullptr, 0x4c43);
#endif
    m_bound = false;
}
bool GlobalHotkey::bind(const QKeySequence& sequence, QString& error) {
    unbind(); if (sequence.isEmpty()) return true;
#ifdef Q_OS_WIN
    const auto combination = sequence[0]; const auto modifiers = combination.keyboardModifiers(); const int key = combination.key();
    UINT nativeKey = 0;
    if ((key >= Qt::Key_A && key <= Qt::Key_Z) || (key >= Qt::Key_0 && key <= Qt::Key_9)) nativeKey = UINT(key);
    else if (key >= Qt::Key_F1 && key <= Qt::Key_F24) nativeKey = VK_F1 + UINT(key - Qt::Key_F1);
    else if (key == Qt::Key_Space) nativeKey = VK_SPACE;
    else if (key == Qt::Key_Insert) nativeKey = VK_INSERT;
    else if (key == Qt::Key_Delete) nativeKey = VK_DELETE;
    else if (key == Qt::Key_Home) nativeKey = VK_HOME;
    else if (key == Qt::Key_End) nativeKey = VK_END;
    else if (key == Qt::Key_PageUp) nativeKey = VK_PRIOR;
    else if (key == Qt::Key_PageDown) nativeKey = VK_NEXT;
    if (!nativeKey) { error = QStringLiteral("Ce raccourci n'est pas pris en charge. Choisissez par exemple Ctrl+Alt+F10."); return false; }
    UINT flags = MOD_NOREPEAT;
    if (modifiers & Qt::ControlModifier) flags |= MOD_CONTROL;
    if (modifiers & Qt::AltModifier) flags |= MOD_ALT;
    if (modifiers & Qt::ShiftModifier) flags |= MOD_SHIFT;
    if (modifiers & Qt::MetaModifier) flags |= MOD_WIN;
    if (!RegisterHotKey(nullptr, 0x4c43, flags, nativeKey)) { error = QStringLiteral("Le raccourci est déjà utilisé ou réservé par Windows. Choisissez-en un autre."); return false; }
    m_bound = true; return true;
#else
    error = QStringLiteral("Les raccourcis globaux Linux seront intégrés avec le portail KDE."); return false;
#endif
}
bool GlobalHotkey::nativeEventFilter(const QByteArray&, void* message, qintptr* result) {
#ifdef Q_OS_WIN
    const auto* event = static_cast<MSG*>(message);
    if (m_bound && event->message == WM_HOTKEY && event->wParam == 0x4c43) { if (result) *result = 0; emit activated(); return true; }
#else
    Q_UNUSED(message); Q_UNUSED(result);
#endif
    return false;
}
}
