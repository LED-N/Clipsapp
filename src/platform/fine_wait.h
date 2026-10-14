#pragma once
#include <QtGlobal>
#include <chrono>
#include <thread>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
namespace clipsapp {
// Windows wakes a sleeping thread every 15.6 ms by default: too coarse to hand out or to take
// an image every 16.7 ms. The wait goes through a high-resolution timer instead, which holds its
// precision whatever the rest of the system asks for, and costs nothing once released.
class FineWait {
public:
    explicit FineWait(bool wanted) {
#ifdef Q_OS_WIN
        // CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, Windows 10 1803 and later.
        if (wanted) m_timer = CreateWaitableTimerExW(nullptr, nullptr, 0x00000002, TIMER_ALL_ACCESS);
#else
        Q_UNUSED(wanted);
#endif
    }
    ~FineWait() {
#ifdef Q_OS_WIN
        if (m_timer) CloseHandle(m_timer);
#endif
    }
    FineWait(const FineWait&) = delete;
    FineWait& operator=(const FineWait&) = delete;
    void wait(int milliseconds) const { waitTicks(10000LL * milliseconds); }
    // In units of 100 ns, the clock's own: at 144 images per second a millisecond is already a seventh of an image.
    void waitTicks(qint64 ticks) const {
        if (ticks <= 0) return;
#ifdef Q_OS_WIN
        LARGE_INTEGER due; due.QuadPart = -ticks;
        if (m_timer && SetWaitableTimer(m_timer, &due, 0, nullptr, nullptr, FALSE)) { WaitForSingleObject(m_timer, 100); return; }
#endif
        std::this_thread::sleep_for(std::chrono::nanoseconds(ticks * 100));
    }
private:
#ifdef Q_OS_WIN
    HANDLE m_timer = nullptr;
#endif
};
}
