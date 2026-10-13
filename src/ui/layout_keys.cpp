#include "layout_keys.h"
#include <QGuiApplication>
#include <QInputMethod>
#include <QKeyEvent>
#include <QLocale>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace clipsapp {
namespace {
// Scan codes of the two keys, the same on every keyboard: first and third of the top letter row.
constexpr quint32 backScan = 0x10, forwardScan = 0x12;
void assign(LayoutKey from, Qt::Key& key, QString& label) {
    const QChar typed(char16_t(from.character & 0xFFFF)); const bool dead = from.character & 0x80000000u;
    if (from.virtualKey >= 'A' && from.virtualKey <= 'Z') {
        // A letter key. Layouts that type another alphabet keep the Latin key for the shortcut and show their own letter.
        key = Qt::Key(from.virtualKey); label = !dead && typed.isLetter() ? QString(typed.toUpper()) : QString(QChar(char16_t(from.virtualKey)));
    } else if (!dead && typed.unicode() > 0x20 && typed.unicode() < 0x7F) { key = Qt::Key(typed.toUpper().unicode()); label = QString(typed.toUpper()); }
}
}
StepKeys stepKeysFor(LayoutKey back, LayoutKey forward) {
    StepKeys keys; assign(back, keys.back, keys.backLabel); assign(forward, keys.forward, keys.forwardLabel);
    return keys.back == keys.forward ? StepKeys{} : keys;
}
StepKeys stepKeys() {
#ifdef Q_OS_WIN
    const HKL layout = GetKeyboardLayout(0);
    const auto at = [layout](quint32 scan) { const UINT key = MapVirtualKeyExW(scan, MAPVK_VSC_TO_VK, layout); return LayoutKey{key, key ? MapVirtualKeyExW(key, MAPVK_VK_TO_CHAR, layout) : 0}; };
    return stepKeysFor(at(backScan), at(forwardScan));
#else
    // Without the layout itself, its language tells AZERTY (France, Belgium) from the others.
    const auto locale = QGuiApplication::inputMethod()->locale();
    const bool azerty = locale.language() == QLocale::French && (locale.territory() == QLocale::France || locale.territory() == QLocale::Belgium);
    return stepKeysFor({quint32(azerty ? 'A' : 'Q'), quint32(azerty ? 'a' : 'q')}, {quint32('E'), quint32('e')});
#endif
}
int frameStep(const QKeyEvent* event) {
    if (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) return 0;
#ifdef Q_OS_WIN
    // The place of the key decides when Windows gives it, whatever letter the layout types there.
    if (const auto scan = event->nativeScanCode()) return scan == backScan ? -1 : scan == forwardScan ? 1 : 0;
#endif
    const auto keys = stepKeys();
    return event->key() == keys.back ? -1 : event->key() == keys.forward ? 1 : 0;
}
}
