#pragma once
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
    qint64 discordLimitBytes = 20000000;
    QString discordCodec = "h264";
    bool discordAutoResize = true;
    // Listening level of the full-screen player, in percent. It changes nothing in the clips.
    int playbackVolume = 100;
    // Ask GitHub at launch whether a newer version was published.
    bool checkUpdates = true;
    static AppConfig defaults();
    RecordingProfile* activeProfile();
    const RecordingProfile* activeProfile() const;
    QStringList validationErrors() const;
    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject& object, AppConfig& result, QString& error);
};
QString newId();
QStringList applicationExclusions(const AudioSource& source, const RecordingProfile& profile);
double estimatedBufferMiB(const RecordingProfile& profile);
}

