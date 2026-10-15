#pragma once
#include <QString>

namespace clipsapp {
// When the replay starts and stops by itself. The window tells it what runs and what the user
// did, and does what it answers: nothing here touches the capture.
class AutoReplay {
public:
    enum class Action { None, Start, Stop };
    // How long the replay outlives the last game, so that what just happened can still be saved.
    static constexpr qint64 graceMs = 30000;
    // "off", "launch" or "games", as AppConfig::autoReplay.
    void setMode(const QString& mode) { m_mode = mode; }
    QString mode() const { return m_mode; }
    // Once, as the application starts.
    Action launched() const { return m_mode == QLatin1String("launch") ? Action::Start : Action::None; }
    // Every few seconds: whether one of the listed games runs, and whether a replay runs or is on its way.
    Action tick(bool gameRunning, bool replayBusy, qint64 nowMs);
    // The replay started, by this policy or by the user.
    void started(bool automatic) { m_automatic = automatic; m_goneSince = -1; }
    // Stopped or failed: a game that still runs does not start it again. Closing that game lifts the hold.
    void stoppedByUser() { m_automatic = false; m_held = true; m_goneSince = -1; }
    void failed() { stoppedByUser(); }
    // Whether the replay that runs was started here, and so will be stopped here.
    bool automatic() const { return m_automatic; }
    bool held() const { return m_held; }
private:
    QString m_mode = QStringLiteral("off");
    bool m_automatic = false, m_held = false;
    qint64 m_goneSince = -1;
};
}
