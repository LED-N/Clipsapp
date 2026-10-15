#include "auto_replay.h"

namespace clipsapp {
AutoReplay::Action AutoReplay::tick(bool gameRunning, bool replayBusy, qint64 nowMs) {
    // Leaving the mode leaves the replay as it is: the user changed a setting, not the game.
    if (m_mode != QLatin1String("games")) { m_held = false; m_goneSince = -1; return Action::None; }
    if (gameRunning) {
        m_goneSince = -1;
        return replayBusy || m_held ? Action::None : Action::Start;
    }
    m_held = false;
    if (!replayBusy) { m_automatic = false; m_goneSince = -1; return Action::None; }
    // A replay the user started is the user's to stop.
    if (!m_automatic) return Action::None;
    if (m_goneSince < 0) { m_goneSince = nowMs; return Action::None; }
    if (nowMs - m_goneSince < graceMs) return Action::None;
    m_automatic = false; m_goneSince = -1; return Action::Stop;
}
}
