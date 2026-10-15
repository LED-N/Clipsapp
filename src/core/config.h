#pragma once
#include "core/track_mix.h"
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace clipsapp {
enum class SourceKind { Microphone, Application, Output, ApplicationMix };
QString sourceKindKey(SourceKind kind);
QString sourceKindLabel(SourceKind kind);

struct AudioTrack { QString id; QString name; };
struct AudioSource {
    QString id;
    QString name;
    SourceKind kind = SourceKind::Microphone;
    QString targetId;
    QString targetLabel;
    QString trackId;
    bool enabled = true;
    QStringList excludedApplications;
    bool excludeSeparateApplications = true;
};
struct VideoSettings {
    QString targetId;
    QString targetLabel;
    int width = 1920;
    int height = 1080;
    int fps = 60;
    int bitrateMbps = 25;
    // "bitrate" aims at bitrateMbps. "quality" holds one level of quality and spends what each
    // scene needs, with bitrateMbps as its ceiling.
    QString rateControl = "bitrate";
    // Quantizer of the "quality" mode: lower stays closer to the screen, and weighs more.
    int quality = 20;
    // "fast" asks the least of the GPU; "thorough" compresses better at the same bitrate.
    QString effort = "fast";
    QString codec = "h264";
    QString encoder = "auto";
    bool captureCursor = false;
};
struct ReplaySettings { int durationSeconds = 60; int memoryLimitMiB = 512; QString hotkey = "Ctrl+Alt+F10"; };
struct RecordingProfile {
    QString id;
    QString name;
    VideoSettings video;
    ReplaySettings replay;
    QList<AudioTrack> tracks;
    QList<AudioSource> sources;
    QString gameName;
    bool detectGame = true;
};
struct AppConfig {
    QString libraryPath;
    QString activeProfileId;
    QList<RecordingProfile> profiles;
    bool clipSavedSound = true, clipSavedNotification = true;
    // Which sound a saved clip makes, and how loud, from 0 to 100: 50 is the level of the earlier versions.
    QString clipSavedSoundId = "duo";
    int clipSavedSoundVolume = 50;
    qint64 discordLimitBytes = 20000000;
    QString discordCodec = "h264";
    bool discordAutoResize = true;
    // Listening level of the full-screen player, in percent. It changes nothing in the clips.
    int playbackVolume = 100;
    // Ask GitHub at launch whether a newer version was published.
    bool checkUpdates = true;
    // Closing the window keeps the application, and its replay, behind the notification area.
    // The first time, a notification says where it went.
    bool closeToTray = true, trayNoticeShown = false;
    // A shortcut to the application is kept in the Start menu.
    bool startMenuShortcut = false;
    // Started with the Windows session, the application waits behind its icon.
    bool launchAtLogin = false;
    // When the replay starts by itself: never ("off"), as soon as the application runs ("launch"),
    // or while one of the games listed runs ("games"), until a little after the last one closes.
    QString autoReplay = "off";
    QStringList replayGames;
    // The usual level of each track, applied when a clip is played, edited or shared, never when
    // it is recorded. Then what the application learns it from: the levels of the last exports,
    // and the proposal the user refused.
    TrackMix defaultMix;
    QList<TrackMix> mixHistory;
    TrackMix mixDeclined;
    // The same for the clips of a game, by its name: its levels over the usual ones, the exports of
    // its clips they are learnt from, and the proposal refused for it. A clip without a game teaches
    // the usual levels; a clip of a game teaches those of its game only.
    QMap<QString, TrackMix> gameMixes;
    QMap<QString, QList<TrackMix>> gameMixHistory;
    QMap<QString, TrackMix> gameMixDeclined;
    // Screenshots: the shortcut that takes one of the captured screen, whether the replay runs or
    // not, and the sound it makes.
    QString screenshotHotkey = "Ctrl+Alt+F11";
    bool screenshotSound = true;
    QString screenshotSoundId = "shutter";
    int screenshotSoundVolume = 50;
    static AppConfig defaults();
    RecordingProfile* activeProfile();
    const RecordingProfile* activeProfile() const;
    QStringList validationErrors() const;
    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject& object, AppConfig& result, QString& error);
};
QString newId();
QStringList applicationExclusions(const AudioSource& source, const RecordingProfile& profile);
// What the replay holds at the bitrate of the profile: its weight on a target, its most in constant quality.
double estimatedBufferMiB(const RecordingProfile& profile);
// What it usually holds, from a calm game to a busy one. Both ends are the same on a target bitrate.
struct BufferRange { double low = 0, high = 0; };
BufferRange usualBufferMiB(const RecordingProfile& profile);
}

