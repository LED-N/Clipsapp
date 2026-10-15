#pragma once
#include <QList>
#include <QMap>
#include <QString>
#include <optional>
namespace clipsapp {
// The usual level of each audio track, by its name: 1 leaves it as recorded. It is where every clip
// starts in the editor, the viewer and the copies made for sharing. Nothing of it is recorded:
// the clip on disk keeps each track at full level.
using TrackMix = QMap<QString, double>;
// Tracks are matched by name, whatever the case or the spaces around it.
QString mixKey(const QString& trackName);
double mixLevel(const TrackMix& mix, const QString& trackName);
// Levels differ when one of them moved by a percent or more, on either side.
bool sameMix(const TrackMix& left, const TrackMix& right);

// The application learns the mix from the levels chosen at each export, most recent last. When
// the last three agree on a track, on the same side of its usual level and close to one another,
// their median becomes a proposal. Nothing is proposed again that was refused, unless the habit
// moved away from it.
std::optional<TrackMix> suggestedMix(const QList<TrackMix>& history, const TrackMix& current, const TrackMix& declined);
// Keeps the few exports the proposal is drawn from.
void rememberExport(QList<TrackMix>& history, const TrackMix& levels);
// The usual levels of the clips of a game: those set for that game, and for the tracks it does not
// name, the usual levels of every clip. A clip without a game has the usual levels.
TrackMix mixForGame(const TrackMix& general, const QMap<QString, TrackMix>& games, const QString& game);
}
