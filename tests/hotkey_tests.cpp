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
};
QTEST_MAIN(HotkeyTests)
#include "hotkey_tests.moc"
