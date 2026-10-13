#include "config.h"
#include <QDir>
#include <QJsonArray>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>
#include <algorithm>

namespace clipsapp {
QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
QString sourceKindKey(SourceKind kind) {
    switch (kind) {
    case SourceKind::Microphone: return "microphone";
    case SourceKind::Application: return "application";
    case SourceKind::Output: return "output";
    case SourceKind::ApplicationMix: return "applicationMix";
    }
    return {};
}
QString sourceKindLabel(SourceKind kind) {
    switch (kind) {
    case SourceKind::Microphone: return QStringLiteral("Microphone");
    case SourceKind::Application: return QStringLiteral("Application");
    case SourceKind::Output: return QStringLiteral("Sortie audio");
    case SourceKind::ApplicationMix: return QStringLiteral("Applications sauf exclusions");
    }
    return {};
}
AppConfig AppConfig::defaults() {
    AppConfig config;
    const QString movies = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    config.libraryPath = QDir(movies.isEmpty() ? QDir::homePath() : movies).filePath("Clipsapp");
    RecordingProfile profile;
    profile.id = newId();
    profile.name = QStringLiteral("Principal");
    for (const auto& name : {QStringLiteral("Micro"), QStringLiteral("Jeu"), QStringLiteral("Discord")}) {
        AudioTrack track {newId(), name};
        profile.tracks.append(track);
        AudioSource source;
        source.id = newId(); source.name = name; source.trackId = track.id;
        source.kind = name == QStringLiteral("Micro") ? SourceKind::Microphone : SourceKind::Application;
        source.enabled = source.kind == SourceKind::Microphone;
        if (name == QStringLiteral("Jeu")) {
            source.kind = SourceKind::ApplicationMix; source.enabled = true;
            source.targetId = "@applications"; source.targetLabel = QStringLiteral("Toutes les applications sauf exclusions");
        }
        if (source.kind == SourceKind::Microphone) {
            source.targetId = "@default";
            source.targetLabel = QStringLiteral("Microphone par défaut");
        }
        profile.sources.append(source);
    }
    config.activeProfileId = profile.id;
    config.profiles.append(profile);
    return config;
}
RecordingProfile* AppConfig::activeProfile() {
    for (auto& profile : profiles) if (profile.id == activeProfileId) return &profile;
    return nullptr;
}
const RecordingProfile* AppConfig::activeProfile() const {
    for (const auto& profile : profiles) if (profile.id == activeProfileId) return &profile;
    return nullptr;
}
double estimatedBufferMiB(const RecordingProfile& profile) {
    QSet<QString> activeTracks;
    for (const auto& source : profile.sources) if (source.enabled) activeTracks.insert(source.trackId);
    const double bitrate = profile.video.bitrateMbps * 1000000.0 + activeTracks.size() * 192000.0;
    return bitrate * profile.replay.durationSeconds / 8.0 / 1048576.0;
}
QStringList AppConfig::validationErrors() const {
    QStringList errors;
    if (libraryPath.trimmed().isEmpty() || !QDir::isAbsolutePath(libraryPath)) errors << QStringLiteral("Choisissez un dossier de clips avec un chemin absolu.");
    if (profiles.isEmpty() || !activeProfile()) errors << QStringLiteral("Le profil actif est absent.");
    QSet<QString> profileIds;
    for (const auto& profile : profiles) {
        const QString prefix = profile.name + ": ";
        if (profile.id.isEmpty() || profileIds.contains(profile.id)) errors << QStringLiteral("Identifiant de profil absent ou dupliqué.");
        profileIds.insert(profile.id);
        if (profile.name.trimmed().isEmpty()) errors << QStringLiteral("Un profil doit avoir un nom.");
        if (profile.video.fps != 30 && profile.video.fps != 60) errors << prefix + QStringLiteral("La fréquence doit être de 30 ou 60 FPS.");
        const auto& v = profile.video;
        if (!((v.width == 1920 && v.height == 1080) || (v.width == 2560 && v.height == 1440) || (v.width == 3840 && v.height == 2160))) errors << prefix + QStringLiteral("Résolution non prise en charge.");
        if (v.bitrateMbps < 5 || v.bitrateMbps > 150) errors << prefix + QStringLiteral("Le débit doit être compris entre 5 et 150 Mbit/s.");
        if (v.codec != "h264" && v.codec != "hevc") errors << prefix + QStringLiteral("Codec inconnu.");
        if (!QStringList{"auto", "nvenc", "amf", "qsv", "vaapi"}.contains(v.encoder)) errors << prefix + QStringLiteral("Encodeur inconnu.");
        if (profile.replay.durationSeconds < 15 || profile.replay.durationSeconds > 600) errors << prefix + QStringLiteral("La durée doit être comprise entre 15 et 600 secondes.");
        if (profile.replay.memoryLimitMiB < 128 || profile.replay.memoryLimitMiB > 8192) errors << prefix + QStringLiteral("La limite de mémoire doit être comprise entre 128 et 8192 Mio.");
        QSet<QString> trackIds;
        for (const auto& track : profile.tracks) {
            if (track.id.isEmpty() || trackIds.contains(track.id) || track.name.trimmed().isEmpty()) errors << prefix + QStringLiteral("Une piste audio est invalide.");
            trackIds.insert(track.id);
        }
        QSet<QString> sourceIds;
        int automaticSources = 0;
        for (const auto& source : profile.sources) {
            if (source.id.isEmpty() || sourceIds.contains(source.id) || source.name.trimmed().isEmpty()) errors << prefix + QStringLiteral("Une source audio est invalide.");
            sourceIds.insert(source.id);
            if (!trackIds.contains(source.trackId)) errors << prefix + QStringLiteral("Une source référence une piste absente.");
            if (source.kind == SourceKind::ApplicationMix && source.enabled) ++automaticSources;
            if (source.excludedApplications.size() > 64) errors << prefix + QStringLiteral("Une source ne peut pas exclure plus de 64 applications.");
        }
        if (automaticSources > 1) errors << prefix + QStringLiteral("Utilisez une seule source automatique pour éviter de doubler le son des applications.");
    }
    return errors;
}
QJsonObject AppConfig::toJson() const {
    QJsonArray profileArray;
    for (const auto& p : profiles) {
        QJsonArray tracks, sources;
        for (const auto& t : p.tracks) tracks.append(QJsonObject{{"id", t.id}, {"name", t.name}});
        for (const auto& s : p.sources) sources.append(QJsonObject{{"id", s.id}, {"name", s.name}, {"kind", sourceKindKey(s.kind)}, {"targetId", s.targetId}, {"targetLabel", s.targetLabel}, {"trackId", s.trackId}, {"enabled", s.enabled}, {"excludedApplications", QJsonArray::fromStringList(s.excludedApplications)}, {"excludeSeparateApplications", s.excludeSeparateApplications}});
        profileArray.append(QJsonObject{
            {"id", p.id}, {"name", p.name}, {"gameName", p.gameName}, {"detectGame", p.detectGame},
            {"video", QJsonObject{{"targetId", p.video.targetId}, {"targetLabel", p.video.targetLabel}, {"width", p.video.width}, {"height", p.video.height}, {"fps", p.video.fps}, {"bitrateMbps", p.video.bitrateMbps}, {"codec", p.video.codec}, {"encoder", p.video.encoder}, {"captureCursor", p.video.captureCursor}}},
            {"replay", QJsonObject{{"durationSeconds", p.replay.durationSeconds}, {"memoryLimitMiB", p.replay.memoryLimitMiB}, {"hotkey", p.replay.hotkey}}},
            {"tracks", tracks}, {"sources", sources}
        });
    }
    return {{"schemaVersion", 1}, {"audioRoutingVersion", 2}, {"libraryPath", libraryPath}, {"activeProfileId", activeProfileId}, {"profiles", profileArray}, {"clipSavedSound", clipSavedSound}, {"clipSavedNotification", clipSavedNotification}, {"playbackVolume", playbackVolume}, {"checkUpdates", checkUpdates}, {"discord", QJsonObject{{"limitBytes", discordLimitBytes}, {"codec", discordCodec}, {"autoResize", discordAutoResize}}}};
}
bool AppConfig::fromJson(const QJsonObject& object, AppConfig& result, QString& error) {
    if (object.value("schemaVersion").toInt(-1) != 1) { error = QStringLiteral("Version du fichier de configuration non prise en charge."); return false; }
    AppConfig candidate;
    candidate.libraryPath = object.value("libraryPath").toString();
    candidate.activeProfileId = object.value("activeProfileId").toString();
    candidate.clipSavedSound = object.value("clipSavedSound").toBool(true); candidate.clipSavedNotification = object.value("clipSavedNotification").toBool(true);
    candidate.playbackVolume = std::clamp(object.value("playbackVolume").toInt(100), 0, 100); candidate.checkUpdates = object.value("checkUpdates").toBool(true);
    const auto discord = object.value("discord").toObject(); candidate.discordLimitBytes = discord.value("limitBytes").toInteger(20000000); candidate.discordCodec = discord.value("codec").toString("h264"); candidate.discordAutoResize = discord.value("autoResize").toBool(true);
    if (candidate.discordLimitBytes < 100000 || candidate.discordLimitBytes > 10000000000LL || !QStringList{"h264", "hevc", "av1"}.contains(candidate.discordCodec)) { error = QStringLiteral("Réglages de partage Discord invalides."); return false; }
    for (const auto& value : object.value("profiles").toArray()) {
        const auto obj = value.toObject();
        RecordingProfile p;
        p.id = obj.value("id").toString(); p.name = obj.value("name").toString();
        p.gameName = obj.value("gameName").toString().left(80); p.detectGame = obj.value("detectGame").toBool(true);
        const auto v = obj.value("video").toObject();
        p.video.targetId = v.value("targetId").toString(); p.video.targetLabel = v.value("targetLabel").toString();
        p.video.width = v.value("width").toInt(1920); p.video.height = v.value("height").toInt(1080);
        p.video.fps = v.value("fps").toInt(60); p.video.bitrateMbps = v.value("bitrateMbps").toInt(25);
        p.video.codec = v.value("codec").toString("h264"); p.video.encoder = v.value("encoder").toString("auto");
        p.video.captureCursor = v.value("captureCursor").toBool(false);
        const auto r = obj.value("replay").toObject();
        p.replay.durationSeconds = r.value("durationSeconds").toInt(60); p.replay.memoryLimitMiB = r.value("memoryLimitMiB").toInt(512); p.replay.hotkey = r.value("hotkey").toString("Ctrl+Alt+F10");
        for (const auto& track : obj.value("tracks").toArray()) {
            const auto t = track.toObject(); p.tracks.append({t.value("id").toString(), t.value("name").toString()});
        }
        for (const auto& source : obj.value("sources").toArray()) {
            const auto s = source.toObject(); AudioSource a;
            a.id = s.value("id").toString(); a.name = s.value("name").toString();
            const QString kind = s.value("kind").toString();
            if (kind == "microphone") a.kind = SourceKind::Microphone;
            else if (kind == "application") a.kind = SourceKind::Application;
            else if (kind == "output") a.kind = SourceKind::Output;
            else if (kind == "applicationMix") a.kind = SourceKind::ApplicationMix;
            else { error = QStringLiteral("Type de source audio inconnu."); return false; }
            a.targetId = s.value("targetId").toString(); a.targetLabel = s.value("targetLabel").toString(); a.trackId = s.value("trackId").toString();
            a.enabled = s.value("enabled").toBool(true);
            for (const auto& excluded : s.value("excludedApplications").toArray()) a.excludedApplications.append(excluded.toString());
            a.excludeSeparateApplications = s.value("excludeSeparateApplications").toBool(true);
            if (object.value("audioRoutingVersion").toInt(1) < 2 && a.kind == SourceKind::Application && a.name == QStringLiteral("Jeu")) {
                a.kind = SourceKind::ApplicationMix; a.targetId = "@applications";
                a.targetLabel = QStringLiteral("Toutes les applications sauf exclusions"); a.enabled = true;
            }
            p.sources.append(a);
        }
        candidate.profiles.append(p);
    }
    const auto errors = candidate.validationErrors();
    if (!errors.isEmpty()) { error = errors.join('\n'); return false; }
    result = candidate; error.clear(); return true;
}
QStringList applicationExclusions(const AudioSource& source, const RecordingProfile& profile) {
    QStringList result = source.excludedApplications;
    if (source.excludeSeparateApplications) for (const auto& other : profile.sources)
        if (other.enabled && other.kind == SourceKind::Application && other.id != source.id && !other.targetId.isEmpty()) result.append(other.targetId);
    for (auto& path : result) path = QDir::fromNativeSeparators(path).toCaseFolded();
    result.removeDuplicates(); result.removeAll(QString()); return result;
}
}

