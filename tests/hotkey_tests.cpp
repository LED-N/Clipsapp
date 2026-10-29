#include <QtTest>
#include "platform/global_hotkey.h"
#include <windows.h>
using namespace clipsapp;
class HotkeyTests final : public QObject {
    Q_OBJECT
private slots:
    void registeredWindowsHotkeyReachesQtAndUnbinds() {
        GlobalHotkey hotkey; QString error; QSignalSpy spy(&hotkey, &GlobalHotkey::activated);
        QVERIFY2(hotkey.bind(QKeySequence("Ctrl+Alt+F24"), error), qPrintable(error));
        // Post the Windows notification directly; never synthesize user keystrokes.
        QVERIFY(PostThreadMessageW(GetCurrentThreadId(), WM_HOTKEY, 0x4c43, 0));
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 1000);
        hotkey.unbind(); QVERIFY(PostThreadMessageW(GetCurrentThreadId(), WM_HOTKEY, 0x4c43, 0));
        QTest::qWait(30); QCOMPARE(spy.count(), 1);
        QVERIFY(RegisterHotKey(nullptr, 0x4c44, MOD_CONTROL | MOD_ALT, VK_F24)); UnregisterHotKey(nullptr, 0x4c44);
    }
    void keypadAndPunctuationHaveTheirWindowsKeys() {
        // The keypad, as the settings keep it and read it back.
        const auto plus = QKeySequence::fromString(QKeySequence(Qt::ControlModifier | Qt::KeypadModifier | Qt::Key_Plus).toString(QKeySequence::PortableText), QKeySequence::PortableText);
        QCOMPARE(plus[0].keyboardModifiers(), Qt::ControlModifier | Qt::KeypadModifier); QCOMPARE(windowsVirtualKey(plus[0]), unsigned(VK_ADD));
        QCOMPARE(windowsVirtualKey(QKeyCombination(Qt::KeypadModifier, Qt::Key_Minus)), unsigned(VK_SUBTRACT));
        QCOMPARE(windowsVirtualKey(QKeyCombination(Qt::KeypadModifier, Qt::Key_Asterisk)), unsigned(VK_MULTIPLY));
        QCOMPARE(windowsVirtualKey(QKeyCombination(Qt::KeypadModifier, Qt::Key_7)), unsigned(VK_NUMPAD7));
        QCOMPARE(windowsVirtualKey(QKeyCombination(Qt::NoModifier, Qt::Key_7)), unsigned('7'));
        QCOMPARE(windowsVirtualKey(QKeyCombination(Qt::ControlModifier, Qt::Key_F13)), unsigned(VK_F13));
        QCOMPARE(windowsVirtualKey(QKeyCombination(Qt::NoModifier, Qt::Key_Pause)), unsigned(VK_PAUSE));
        // A character takes the key that writes it on the keyboard in use, whatever it is.
        const SHORT comma = VkKeyScanW(L','); if (comma != -1) QCOMPARE(windowsVirtualKey(QKeyCombination(Qt::AltModifier, Qt::Key_Comma)), unsigned(LOBYTE(comma)));
        QVERIFY(windowsVirtualKey(QKeyCombination(Qt::NoModifier, Qt::Key_unknown)) == 0);
        // A key of the keypad can be held by Windows, like any other.
        GlobalHotkey hotkey; QString error; QVERIFY2(hotkey.bind(QKeySequence(Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier | Qt::KeypadModifier | Qt::Key_Plus), error), qPrintable(error));
    }
};
QTEST_MAIN(HotkeyTests)
#include "hotkey_tests.moc"
