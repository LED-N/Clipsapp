#include "config.h"
#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>
#include <algorithm>
#include <cmath>

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
namespace {
double bufferMiB(const RecordingProfile& profile, double videoMbps) {
    QSet<QString> activeTracks;
    for (const auto& source : profile.sources) if (source.enabled) activeTracks.insert(source.trackId);
    const double bitrate = videoMbps * 1000000.0 + activeTracks.size() * 192000.0;
    return bitrate * profile.replay.durationSeconds / 8.0 / 1048576.0;
}
}
double estimatedBufferMiB(const RecordingProfile& profile) { return bufferMiB(profile, profile.video.bitrateMbps); }
BufferRange usualBufferMiB(const RecordingProfile& profile) {
    const auto& video = profile.video; const double ceiling = estimatedBufferMiB(profile);
    if (video.rateControl != "quality") return {ceiling, ceiling};
    // Measured at level 20 in 1440p at 60 FPS: 56 Mbit/s on a calm game, 91 on a busy one, and
    // twice as much every 5.5 levels down. Other sizes follow the three-quarter power of the
    // number of pixels per second, the usual rule for video. An order of magnitude, not a promise.
    const double scale = std::pow(double(video.width) * video.height * video.fps / (2560.0 * 1440 * 60), 0.75) * std::pow(2.0, (20 - video.quality) / 5.5);
    return {std::min(ceiling, bufferMiB(profile, 56 * scale)), std::min(ceiling, bufferMiB(profile, 91 * scale))};
}
QString replayDiskFolder(const ReplaySettings& replay) {
    return replay.diskFolder.isEmpty() ? QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("Buffer du replay")) : replay.diskFolder;
}
bool suggestsDiskBuffer(const RecordingProfile& profile, qint64 physicalMemoryBytes) {
    return physicalMemoryBytes > 0 && usualBufferMiB(profile).high * 1048576.0 > diskBufferMemoryShare * double(physicalMemoryBytes);
}
QStringList AppConfig::validationErrors() const {
    QStringList errors;
    if (libraryPath.trimmed().isEmpty() || !QDir::isAbsolutePath(libraryPath)) errors << QStringLiteral("Choisissez un dossier de clips avec un chemin absolu.");
    if (!QStringList{"off", "launch", "games"}.contains(autoReplay)) errors << QStringLiteral("Mode de replay automatique inconnu.");
    if (replayGames.size() > 64) errors << QStringLiteral("Le replay automatique suit 64 jeux au plus.");
    if (gameMixes.size() > 200) errors << QStringLiteral("Les volumes habituels sont retenus pour 200 jeux au plus.");
    if (screenshotSoundVolume < 0 || screenshotSoundVolume > 100) errors << QStringLiteral("Le volume du son des screenshots doit être compris entre 0 et 100.");
    if (!recordHotkey.isEmpty() && recordHotkey.compare(screenshotHotkey, Qt::CaseInsensitive) == 0) errors << QStringLiteral("Le raccourci de l'enregistrement doit être différent de celui des screenshots.");
    if (profiles.isEmpty() || !activeProfile()) errors << QStringLiteral("Le profil actif est absent.");
    QSet<QString> profileIds;
    for (const auto& profile : profiles) {
        const QString prefix = profile.name + ": ";
        if (profile.id.isEmpty() || profileIds.contains(profile.id)) errors << QStringLiteral("Identifiant de profil absent ou dupliqué.");
        profileIds.insert(profile.id);
        if (profile.name.trimmed().isEmpty()) errors << QStringLiteral("Un profil doit avoir un nom.");
        if (!QList<int>{30, 60, 120, 144}.contains(profile.video.fps)) errors << prefix + QStringLiteral("La fréquence doit être de 30, 60, 120 ou 144 FPS.");
        const auto& v = profile.video;
        if (!((v.width == 1920 && v.height == 1080) || (v.width == 2560 && v.height == 1440) || (v.width == 3840 && v.height == 2160))) errors << prefix + QStringLiteral("Résolution non prise en charge.");
        if (v.bitrateMbps < 5 || v.bitrateMbps > 300) errors << prefix + QStringLiteral("Le débit doit être compris entre 5 et 300 Mbit/s.");
        if (v.rateControl != "bitrate" && v.rateControl != "quality") errors << prefix + QStringLiteral("Mode de débit inconnu.");
        if (v.quality < 10 || v.quality > 30) errors << prefix + QStringLiteral("Le niveau de qualité doit être compris entre 10 et 30.");
        if (v.effort != "fast" && v.effort != "thorough") errors << prefix + QStringLiteral("Effort d'encodage inconnu.");
        if (v.codec != "h264" && v.codec != "hevc") errors << prefix + QStringLiteral("Codec inconnu.");
        if (!QStringList{"auto", "nvenc", "amf", "vaapi"}.contains(v.encoder)) errors << prefix + QStringLiteral("Encodeur inconnu.");
        if (profile.replay.durationSeconds < 15 || profile.replay.durationSeconds > 600) errors << prefix + QStringLiteral("La durée doit être comprise entre 15 et 600 secondes.");
        if (profile.replay.memoryLimitMiB < 128 || profile.replay.memoryLimitMiB > 8192) errors << prefix + QStringLiteral("La limite de mémoire doit être comprise entre 128 et 8192 Mio.");
        if (profile.replay.storage != "memory" && profile.replay.storage != "disk") errors << prefix + QStringLiteral("Emplacement du buffer inconnu.");
        if (profile.replay.diskLimitMiB < 1024 || profile.replay.diskLimitMiB > 262144) errors << prefix + QStringLiteral("Le plafond du buffer sur disque doit être compris entre 1 et 256 Gio.");
        if (!profile.replay.diskFolder.isEmpty() && !QDir::isAbsolutePath(profile.replay.diskFolder)) errors << prefix + QStringLiteral("Choisissez un dossier du buffer avec un chemin absolu.");
        // One key pressed, one thing done: a shortcut that saved a clip and took a screenshot would do both.
        if (!screenshotHotkey.isEmpty() && profile.replay.hotkey.compare(screenshotHotkey, Qt::CaseInsensitive) == 0) errors << prefix + QStringLiteral("Le raccourci des screenshots doit être différent de celui du replay.");
        if (!recordHotkey.isEmpty() && profile.replay.hotkey.compare(recordHotkey, Qt::CaseInsensitive) == 0) errors << prefix + QStringLiteral("Le raccourci de l'enregistrement doit être différent de celui du replay.");
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
            {"video", QJsonObject{{"targetId", p.video.targetId}, {"targetLabel", p.video.targetLabel}, {"width", p.video.width}, {"height", p.video.height}, {"fps", p.video.fps}, {"bitrateMbps", p.video.bitrateMbps}, {"rateControl", p.video.rateControl}, {"quality", p.video.quality}, {"effort", p.video.effort}, {"codec", p.video.codec}, {"encoder", p.video.encoder}, {"captureCursor", p.video.captureCursor}}},
            {"replay", QJsonObject{{"durationSeconds", p.replay.durationSeconds}, {"memoryLimitMiB", p.replay.memoryLimitMiB}, {"hotkey", p.replay.hotkey}, {"storage", p.replay.storage}, {"diskLimitMiB", p.replay.diskLimitMiB}, {"diskFolder", p.replay.diskFolder}, {"keepMemory", p.replay.keepMemory}}},
            {"tracks", tracks}, {"sources", sources}
        });
    }
    const auto levels = [](const TrackMix& mix) { QJsonObject object; for (auto track = mix.cbegin(); track != mix.cend(); ++track) object.insert(track.key(), track.value()); return object; };
    const auto historyOf = [&levels](const QList<TrackMix>& exports) { QJsonArray array; for (const auto& exported : exports) array.append(levels(exported)); return array; };
    QSet<QString> named; for (auto it = gameMixes.cbegin(); it != gameMixes.cend(); ++it) named.insert(it.key());
    for (auto it = gameMixHistory.cbegin(); it != gameMixHistory.cend(); ++it) named.insert(it.key());
    for (auto it = gameMixDeclined.cbegin(); it != gameMixDeclined.cend(); ++it) named.insert(it.key());
    QJsonObject games; for (const auto& game : named) games.insert(game, QJsonObject{{"levels", levels(gameMixes.value(game))}, {"history", historyOf(gameMixHistory.value(game))}, {"declined", levels(gameMixDeclined.value(game))}});
    return {{"mix", QJsonObject{{"levels", levels(defaultMix)}, {"history", historyOf(mixHistory)}, {"declined", levels(mixDeclined)}, {"games", games}}},
            {"screenshots", QJsonObject{{"hotkey", screenshotHotkey}, {"sound", screenshotSound}, {"soundId", screenshotSoundId}, {"soundVolume", screenshotSoundVolume}}}, {"recording", QJsonObject{{"hotkey", recordHotkey}}},
            {"theme", QJsonObject{{"background", theme.background}, {"accent", theme.accent}, {"text", theme.text}, {"windowGradient", theme.windowGradient}, {"windowEnd", theme.windowEnd}, {"accentGradient", theme.accentGradient}, {"accentEnd", theme.accentEnd}, {"angle", theme.angle}}},{"schemaVersion", 1}, {"audioRoutingVersion", 2}, {"libraryPath", libraryPath}, {"activeProfileId", activeProfileId}, {"profiles", profileArray}, {"clipSavedSound", clipSavedSound}, {"clipSavedSoundId", clipSavedSoundId}, {"clipSavedSoundVolume", clipSavedSoundVolume}, {"clipSavedNotification", clipSavedNotification}, {"playbackVolume", playbackVolume}, {"checkUpdates", checkUpdates}, {"closeToTray", closeToTray}, {"trayNoticeShown", trayNoticeShown}, {"startMenuShortcut", startMenuShortcut}, {"startup", QJsonObject{{"atLogin", launchAtLogin}, {"autoReplay", autoReplay}, {"games", QJsonArray::fromStringList(replayGames)}}}, {"discord", QJsonObject{{"limitBytes", discordLimitBytes}, {"codec", discordCodec}, {"autoResize", discordAutoResize}}}};
}
bool AppConfig::fromJson(const QJsonObject& object, AppConfig& result, QString& error) {
    if (object.value("schemaVersion").toInt(-1) != 1) { error = QStringLiteral("Version du fichier de configuration non prise en charge."); return false; }
    AppConfig candidate;
    candidate.libraryPath = object.value("libraryPath").toString();
    candidate.activeProfileId = object.value("activeProfileId").toString();
    candidate.clipSavedSound = object.value("clipSavedSound").toBool(true); candidate.clipSavedNotification = object.value("clipSavedNotification").toBool(true);
    candidate.clipSavedSoundId = object.value("clipSavedSoundId").toString(QStringLiteral("duo")).left(32); candidate.clipSavedSoundVolume = std::clamp(object.value("clipSavedSoundVolume").toInt(50), 0, 100);
    candidate.playbackVolume = std::clamp(object.value("playbackVolume").toInt(100), 0, 100); candidate.checkUpdates = object.value("checkUpdates").toBool(true);
    candidate.closeToTray = object.value("closeToTray").toBool(true); candidate.trayNoticeShown = object.value("trayNoticeShown").toBool(false); candidate.startMenuShortcut = object.value("startMenuShortcut").toBool(false);
    const auto startup = object.value("startup").toObject(); candidate.launchAtLogin = startup.value("atLogin").toBool(false); candidate.autoReplay = startup.value("autoReplay").toString("off");
    for (const auto& game : startup.value("games").toArray()) if (const auto path = QDir::fromNativeSeparators(game.toString().trimmed()); !path.isEmpty() && !candidate.replayGames.contains(path, Qt::CaseInsensitive)) candidate.replayGames.append(path);
    const auto discord = object.value("discord").toObject(); candidate.discordLimitBytes = discord.value("limitBytes").toInteger(20000000); candidate.discordCodec = discord.value("codec").toString("h264"); candidate.discordAutoResize = discord.value("autoResize").toBool(true);
    // A level outside what the editor allows is not a setting: the track goes back to its recorded level.
    const auto levels = [](const QJsonValue& value) { TrackMix mix; const auto object = value.toObject(); for (auto track = object.constBegin(); track != object.constEnd(); ++track) { const double level = track.value().toDouble(-1); if (level >= 0 && level <= 2 && !mixKey(track.key()).isEmpty()) mix.insert(mixKey(track.key()), level); } return mix; };
    const auto mix = object.value("mix").toObject(); candidate.defaultMix = levels(mix.value("levels")); candidate.mixDeclined = levels(mix.value("declined"));
    for (const auto& exported : mix.value("history").toArray()) rememberExport(candidate.mixHistory, levels(exported));
    const auto games = mix.value("games").toObject();
    for (auto game = games.constBegin(); game != games.constEnd() && candidate.gameMixes.size() < 200; ++game) {
        const auto name = game.key().trimmed().left(80); if (name.isEmpty()) continue; const auto entry = game.value().toObject();
        if (const auto own = levels(entry.value("levels")); !own.isEmpty()) candidate.gameMixes.insert(name, own);
        if (const auto refused = levels(entry.value("declined")); !refused.isEmpty()) candidate.gameMixDeclined.insert(name, refused);
        QList<TrackMix> exports; for (const auto& exported : entry.value("history").toArray()) rememberExport(exports, levels(exported));
        if (!exports.isEmpty()) candidate.gameMixHistory.insert(name, exports);
    }
    const auto screenshots = object.value("screenshots").toObject();
    candidate.screenshotHotkey = screenshots.value("hotkey").toString(QStringLiteral("Ctrl+Alt+F11")); candidate.screenshotSound = screenshots.value("sound").toBool(true);
    candidate.screenshotSoundId = screenshots.value("soundId").toString(QStringLiteral("shutter")).left(32); candidate.screenshotSoundVolume = std::clamp(screenshots.value("soundVolume").toInt(50), 0, 100);
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
        p.video.rateControl = v.value("rateControl").toString("bitrate"); p.video.quality = v.value("quality").toInt(20); p.video.effort = v.value("effort").toString("fast");
        p.video.codec = v.value("codec").toString("h264"); p.video.encoder = v.value("encoder").toString("auto");
        // Intel Quick Sync was offered once and never worked: a profile that chose it goes back to the automatic choice.
        if (p.video.encoder == QLatin1String("qsv")) p.video.encoder = QStringLiteral("auto");
        p.video.captureCursor = v.value("captureCursor").toBool(false);
        const auto r = obj.value("replay").toObject();
        p.replay.durationSeconds = r.value("durationSeconds").toInt(60); p.replay.memoryLimitMiB = r.value("memoryLimitMiB").toInt(512); p.replay.hotkey = r.value("hotkey").toString("Ctrl+Alt+F10");
        p.replay.storage = r.value("storage").toString("memory"); p.replay.diskLimitMiB = r.value("diskLimitMiB").toInt(16384);
        p.replay.diskFolder = QDir::fromNativeSeparators(r.value("diskFolder").toString().trimmed()); p.replay.keepMemory = r.value("keepMemory").toBool(false);
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
    // A file written before screenshots existed may use their default for the replay: they take another.
    if (!screenshots.contains("hotkey")) for (const auto& p : candidate.profiles) if (p.replay.hotkey.compare(candidate.screenshotHotkey, Qt::CaseInsensitive) == 0) candidate.screenshotHotkey = QStringLiteral("Ctrl+Alt+F12");
    // A color that is not one is the default theme's, rather than a file that cannot be read.
    const auto theme = object.value("theme").toObject(); static const QRegularExpression hex(QStringLiteral("^#[0-9A-Fa-f]{6}$"));
    const auto color = [&theme](const char* key) { const auto value = theme.value(QLatin1String(key)).toString().trimmed(); return hex.match(value).hasMatch() ? value.toUpper() : QString(); };
    candidate.theme.background = color("background"); candidate.theme.accent = color("accent"); candidate.theme.text = color("text");
    candidate.theme.windowEnd = color("windowEnd"); candidate.theme.accentEnd = color("accentEnd");
    candidate.theme.windowGradient = theme.value("windowGradient").toBool(false) && !candidate.theme.windowEnd.isEmpty();
    candidate.theme.accentGradient = theme.value("accentGradient").toBool(false) && !candidate.theme.accentEnd.isEmpty();
    candidate.theme.angle = ((theme.value("angle").toInt(135) % 360) + 360) % 360;
    // The same for the recording, which came later still: it takes the first of its keys that is free.
    const auto recording = object.value("recording").toObject();
    candidate.recordHotkey = recording.value("hotkey").toString(QStringLiteral("Ctrl+Alt+F9"));
    if (!recording.contains("hotkey")) for (const auto& key : {QStringLiteral("Ctrl+Alt+F9"), QStringLiteral("Ctrl+Alt+F8"), QStringLiteral("Ctrl+Alt+F7")}) {
        bool taken = key.compare(candidate.screenshotHotkey, Qt::CaseInsensitive) == 0;
        for (const auto& p : candidate.profiles) taken |= key.compare(p.replay.hotkey, Qt::CaseInsensitive) == 0;
        if (!taken) { candidate.recordHotkey = key; break; }
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

