#include "track_mix.h"
#include <QSet>
#include <algorithm>
#include <cmath>

namespace clipsapp {
namespace {
constexpr int agreeing = 3, remembered = 6;
}
QString mixKey(const QString& trackName) { return trackName.trimmed().toCaseFolded(); }
double mixLevel(const TrackMix& mix, const QString& trackName) { return mix.value(mixKey(trackName), 1); }
bool sameMix(const TrackMix& left, const TrackMix& right) {
    QSet<QString> names; for (auto it = left.cbegin(); it != left.cend(); ++it) names.insert(it.key()); for (auto it = right.cbegin(); it != right.cend(); ++it) names.insert(it.key());
    return std::all_of(names.cbegin(), names.cend(), [&](const QString& name) { return std::abs(left.value(name, 1) - right.value(name, 1)) < 0.01; });
}
void rememberExport(QList<TrackMix>& history, const TrackMix& levels) {
    if (levels.isEmpty()) return;
    history.append(levels); while (history.size() > remembered) history.removeFirst();
}
TrackMix mixForGame(const TrackMix& general, const QMap<QString, TrackMix>& games, const QString& game) {
    TrackMix mix = general; if (game.isEmpty()) return mix;
    const auto own = games.constFind(game); if (own == games.constEnd()) return mix;
    for (auto track = own->cbegin(); track != own->cend(); ++track) mix.insert(track.key(), track.value());
    return mix;
}
std::optional<TrackMix> suggestedMix(const QList<TrackMix>& history, const TrackMix& current, const TrackMix& declined) {
    if (history.size() < agreeing) return {};
    const auto recent = history.mid(history.size() - agreeing);
    TrackMix proposal = current; bool changed = false;
    for (auto track = recent.last().cbegin(); track != recent.last().cend(); ++track) {
        QList<double> levels;
        for (const auto& exported : recent) if (exported.contains(track.key())) levels.append(exported.value(track.key()));
        if (levels.size() < agreeing) continue;
        std::sort(levels.begin(), levels.end());
        const double low = levels.first(), high = levels.last(), usual = current.value(track.key(), 1);
        // A habit: every time on the same side of the usual level, by more than a nudge, and within a quarter of one another.
        if (high - low > 0.25 || !(low > usual + 0.04 || high < usual - 0.04)) continue;
        const double level = std::round(levels[agreeing / 2] * 20) / 20;
        if (std::abs(level - usual) < 0.05) continue;
        proposal.insert(track.key(), level); changed = true;
    }
    if (!changed) return {};
    if (!declined.isEmpty()) {
        bool moved = false;
        for (auto track = proposal.cbegin(); track != proposal.cend(); ++track) moved |= std::abs(track.value() - declined.value(track.key(), 1)) > 0.10;
        if (!moved) return {};
    }
    return proposal;
}
}
