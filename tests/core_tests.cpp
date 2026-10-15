#include <QtTest>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include "core/config_store.h"
#include "core/clip_library.h"
#include "core/replay_buffer.h"
#include "core/audio_routing.h"
#include "core/game_identity.h"
#include "core/update.h"
#include "media/clip_edit.h"
#include "platform/start_menu.h"
#include "core/app_log.h"
#include "core/auto_replay.h"
#include "platform/crash_report.h"
#include "platform/folder_watch.h"
#include "platform/game_detection.h"
#include "platform/login_start.h"
#include "platform/uninstall.h"
#include <QScopeGuard>
#include <QSettings>
#include <QSignalSpy>

using namespace clipsapp;
class CoreTests final : public QObject {
    Q_OBJECT
private slots:
    void gameIdentityUsesLocalNamesAndSafeWindowsFolders() {
        QCOMPARE(gameNameFromApplication("D:/Steam/steamapps/common/ARC Raiders/Game/Binaries/Win64/Game-Win64-Shipping.exe", "Unreal", "UE5Game"), QString("ARC Raiders"));
        QCOMPARE(gameNameFromApplication("D:/Games/Custom.exe", "Ma partie · DX12", "Unreal Engine"), QString("Ma partie"));
        QVERIFY(gameNameFromApplication("C:/Discord/Discord.exe", "Discord").isEmpty());
        QCOMPARE(safeGameFolder("../ARC: Raiders. "), QString(".._ARC_ Raiders")); QCOMPARE(safeGameFolder("CON"), QString("_CON")); QCOMPARE(safeGameFolder(".."), QString("Clips"));
        auto config = AppConfig::defaults(); config.profiles.first().gameName = "ARC Raiders"; config.profiles.first().detectGame = false;
        AppConfig loaded; QString error; QVERIFY(AppConfig::fromJson(config.toJson(), loaded, error)); QCOMPARE(loaded.profiles.first().gameName, QString("ARC Raiders")); QVERIFY(!loaded.profiles.first().detectGame);
    }
    void manualLibraryClassificationSurvivesRestartWithoutChangingTheClip() {
        QTemporaryDir directory; const auto clip = directory.filePath("import.mp4"); QFile file(clip); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("original"); file.close();
        const auto path = directory.filePath("config/library-tags.json"); LibraryLabels labels(path); QString error; QVERIFY(labels.load(error));
        QVERIFY(labels.setGame(clip, "ARC Raiders", error)); LibraryLabels reloaded(path); QVERIFY(reloaded.load(error)); QCOMPARE(reloaded.gameFor(clip, "Non classé"), QString("ARC Raiders"));
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), QByteArray("original")); file.close();
        QVERIFY(reloaded.setGame(clip, "", error)); QCOMPARE(reloaded.gameFor(clip, "Mon dossier"), QString("Mon dossier"));
        QFile corrupt(path); QVERIFY(corrupt.open(QIODevice::WriteOnly)); corrupt.write("broken"); corrupt.close(); QVERIFY(!reloaded.load(error)); QVERIFY(!reloaded.setGame(clip, "Autre jeu", error)); QVERIFY(corrupt.open(QIODevice::ReadOnly)); QCOMPARE(corrupt.readAll(), QByteArray("broken"));
    }
    void favouritesShareLinksAndRenamesFollowTheClipAndSurviveRestart() {
        QTemporaryDir directory; const auto clip = directory.filePath("Jeu/Clip.mp4"), copy = directory.filePath("Partage Discord/Clip-0123456789.discord.mp4"); QVERIFY(QDir().mkpath(directory.filePath("Jeu")));
        QFile file(clip); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("original"); file.close();
        const auto path = directory.filePath("config/library-tags.json"); LibraryLabels labels(path); QString error; QVERIFY(labels.load(error));
        QVERIFY(!labels.isFavorite(clip)); QVERIFY(labels.setFavorites({clip}, true, error)); QVERIFY(labels.setGame(clip, "ARC Raiders", error)); QVERIFY(labels.setShareLink(copy, clip, 20000000, error));
        LibraryLabels reloaded(path); QVERIFY(reloaded.load(error)); QVERIFY(reloaded.isFavorite(clip)); QCOMPARE(reloaded.gameFor(clip, {}), QString("ARC Raiders"));
        QCOMPARE(reloaded.shareLink(copy).source, QFileInfo(clip).absoluteFilePath()); QCOMPARE(reloaded.shareLink(copy).limitBytes, qint64(20000000)); QCOMPARE(reloaded.shareCopies(clip).size(), 1); QVERIFY(reloaded.shareLink(clip).source.isEmpty());
        // Renaming keeps the folder and the extension, refuses what is no file name, and never replaces another clip.
        QVERIFY(!renameClipFile(clip, "a/b").error.isEmpty()); QVERIFY(!renameClipFile(clip, "  ").error.isEmpty()); QVERIFY(!renameClipFile(clip, "Copie.discord").error.isEmpty()); QVERIFY(!renameClipFile(directory.filePath("absent.mp4"), "x").error.isEmpty());
        QFile other(directory.filePath("Jeu/Pris.mp4")); QVERIFY(other.open(QIODevice::WriteOnly)); other.write("autre"); other.close();
        QVERIFY(!renameClipFile(clip, "Pris").error.isEmpty()); QVERIFY(QFile::exists(clip)); QCOMPARE(renameClipFile(clip, "Clip").path, QFileInfo(clip).absoluteFilePath());
        const auto renamed = renameClipFile(clip, QStringLiteral(" Clutch été 1v3 ")); QVERIFY2(renamed.error.isEmpty(), qPrintable(renamed.error)); QCOMPARE(renamed.path, directory.filePath(QStringLiteral("Jeu/Clutch été 1v3.mp4")));
        QVERIFY(!QFile::exists(clip)); QFile moved(renamed.path); QVERIFY(moved.open(QIODevice::ReadOnly)); QCOMPARE(moved.readAll(), QByteArray("original")); moved.close();
        QVERIFY(reloaded.moveClip(clip, renamed.path, error)); QVERIFY(reloaded.isFavorite(renamed.path)); QVERIFY(!reloaded.isFavorite(clip)); QCOMPARE(reloaded.gameFor(renamed.path, {}), QString("ARC Raiders")); QCOMPARE(reloaded.shareLink(copy).source, renamed.path);
        // Forgetting a clip drops its marks; forgetting a copy drops its link. A copy outlives its original.
        QVERIFY(reloaded.forget({renamed.path}, error)); QVERIFY(!reloaded.isFavorite(renamed.path)); QCOMPARE(reloaded.gameFor(renamed.path, "Jeu"), QString("Jeu")); QCOMPARE(reloaded.shareLink(copy).source, renamed.path);
        QVERIFY(reloaded.forget({copy}, error)); QVERIFY(reloaded.shareLink(copy).source.isEmpty());
        // A file written by an earlier version, with games only, still loads.
        QFile old(path); QVERIFY(old.open(QIODevice::WriteOnly | QIODevice::Truncate)); old.write(R"({"version":1,"games":{"c:/clips/a.mp4":"Valorant"}})"); old.close();
        LibraryLabels earlier(path); QVERIFY(earlier.load(error)); QCOMPARE(earlier.gameFor("c:/clips/a.mp4", {}), QString("Valorant")); QVERIFY(!earlier.isFavorite("c:/clips/a.mp4"));
    }
    void sizesLimitsAndDateGroupsReadTheWayTheInterfaceWritesThem() {
        QCOMPARE(formatBytes(15800000), QStringLiteral("15,8 Mo")); QCOMPARE(formatBytes(188000000), QStringLiteral("188 Mo")); QCOMPARE(formatBytes(14200000000LL), QStringLiteral("14,2 Go"));
        QCOMPARE(formatLimit(20000000), QStringLiteral("20 Mo")); QCOMPARE(formatLimit(1000000000), QStringLiteral("1 Go")); QCOMPARE(formatLimit(12500000), QStringLiteral("12,5 Mo"));
        const QDate today(2026, 10, 12);
        QCOMPARE(dateGroupTitle(today, today), QStringLiteral("Aujourd’hui")); QCOMPARE(dateGroupTitle(today.addDays(-1), today), QStringLiteral("Hier · dimanche 11 octobre"));
        QCOMPARE(dateGroupTitle(QDate(2026, 10, 10), today), QStringLiteral("Samedi 10 octobre")); QCOMPARE(dateGroupTitle(QDate(2025, 3, 12), today), QStringLiteral("Mercredi 12 mars 2025"));
    }
    void replayBufferStaysBoundedAcrossManyCompleteWindows() {
        ReplayBuffer buffer(60000000, 8 * 1024 * 1024); qint64 maximum = 0;
        for (int frame = 0; frame < 30 * 600; ++frame) {
            const qint64 us = qint64(frame) * 1000000 / 30;
            buffer.push({0, us, us, 33334, frame % 60 == 0, QByteArray(1024 + frame % 500, 'v')});
            buffer.push({1, us - 50000, us - 50000, 33334, true, QByteArray(160, 'a')});
            QVERIFY(buffer.seconds() <= 62.001); QVERIFY(buffer.bytes() <= 8 * 1024 * 1024); maximum = std::max(maximum, buffer.bytes());
        }
        QVERIFY(maximum < 4 * 1024 * 1024); buffer.clear(); QCOMPARE(buffer.bytes(), qint64(0)); QCOMPARE(buffer.seconds(), 0.0);
    }
    void libraryBatchClassificationIsPersistentAndAtomic() {
        QTemporaryDir directory; const auto path = directory.filePath("library-tags.json");
        const QStringList clips{directory.filePath("one.mp4"), directory.filePath("two.mp4")};
        LibraryLabels labels(path); QString error; QVERIFY(labels.load(error));
        QVERIFY(labels.setGames(clips, "FIFA 27", error));
        LibraryLabels reloaded(path); QVERIFY(reloaded.load(error));
        for (const auto& clip : clips) QCOMPARE(reloaded.gameFor(clip, "Non classé"), QString("FIFA 27"));
        QCOMPARE(reloaded.gameNames(), QStringList{"FIFA 27"});
        QVERIFY(reloaded.setGame(clips.first(), {}, error)); QCOMPARE(reloaded.gameFor(clips.first(), "Dossier"), QString("Dossier"));
        QCOMPARE(reloaded.gameFor(clips.last(), "Dossier"), QString("FIFA 27"));
        QFile corrupt(path); QVERIFY(corrupt.open(QIODevice::WriteOnly)); corrupt.write("broken"); corrupt.close();
        QVERIFY(!reloaded.load(error)); QVERIFY(!reloaded.setGames(clips, "Autre", error));
        QVERIFY(corrupt.open(QIODevice::ReadOnly)); QCOMPARE(corrupt.readAll(), QByteArray("broken"));
    }
    void batchDeletionPreservesFailuresAndOnlyProcessesSelectedFilesOnce() {
        QTemporaryDir directory; const auto one = directory.filePath("one.mp4"), two = directory.filePath("two.mp4"), other = directory.filePath("other.mp4");
        for (const auto& path : {one, two, other}) { QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("original"); }
        QStringList attempted;
        const auto result = trashClips({one, two, one, directory.path()}, [&](const QString& path, QString& error) {
            attempted.append(path); if (path == two) { error = "locked"; return false; }
            return QFile::rename(path, path + ".test-trash");
        });
        QCOMPARE(attempted, QStringList({one, two})); QCOMPARE(result.removed, QStringList{one}); QCOMPARE(result.errors.size(), 2);
        QVERIFY(!QFile::exists(one)); QVERIFY(QFile::exists(one + ".test-trash"));
        for (const auto& path : {two, other}) { QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), QByteArray("original")); }
    }
    void automaticAudioExclusionsPersistAndFollowSeparateTracks() {
        auto config = AppConfig::defaults(); auto& profile = config.profiles.first(); auto& game = profile.sources[1];
        QCOMPARE(game.kind, SourceKind::ApplicationMix); QVERIFY(game.enabled);
        game.excludedApplications = {"C:/Music/Player.exe"}; profile.sources[2].targetId = "C:/Discord/Discord.exe"; profile.sources[2].enabled = true;
        auto exclusions = applicationExclusions(game, profile); QCOMPARE(exclusions.size(), 2); QVERIFY(exclusions.contains("c:/discord/discord.exe"));
        profile.sources[2].trackId = game.trackId; QCOMPARE(applicationExclusions(game, profile).size(), 2);
        profile.sources[2].trackId = profile.tracks[2].id; game.excludeSeparateApplications = false; QCOMPARE(applicationExclusions(game, profile).size(), 1);
        AppConfig loaded; QString error; QVERIFY2(AppConfig::fromJson(config.toJson(), loaded, error), qPrintable(error));
        QCOMPARE(loaded.profiles.first().sources[1].excludedApplications, game.excludedApplications); QVERIFY(!loaded.profiles.first().sources[1].excludeSeparateApplications);
    }
    void legacyGameSourceMigratesOnceWithoutChangingOtherSources() {
        auto config = AppConfig::defaults(); config.profiles.first().sources[1].kind = SourceKind::Application;
        config.profiles.first().sources[1].targetId = "C:/Game.exe"; config.profiles.first().sources[1].enabled = false;
        auto json = config.toJson(); json.remove("audioRoutingVersion"); AppConfig loaded; QString error;
        QVERIFY(AppConfig::fromJson(json, loaded, error)); QCOMPARE(loaded.profiles.first().sources[1].kind, SourceKind::ApplicationMix); QVERIFY(loaded.profiles.first().sources[1].enabled);
        loaded.profiles.first().sources[1].kind = SourceKind::Application;
        auto saved = loaded.toJson(); QVERIFY(AppConfig::fromJson(saved, config, error)); QCOMPARE(config.profiles.first().sources[1].kind, SourceKind::Application);
    }
    void audioTreesExcludeSelectedAppsDescendantsAndThePlayerWithoutDoubleCapture() {
        QList<AudioProcess> processes{{10, 1, "C:/Game.exe"}, {11, 10, "C:/GameWorker.exe"}, {20, 1, "C:/Discord/Discord.exe"}, {21, 20, "C:/Helper.exe"}, {30, 1, "C:/Browser.exe"}, {99, 1, "C:/Clipsapp.exe"}, {100, 99, "C:/ffmpeg.exe"}};
        const QList<quint32> sessions{10, 11, 20, 21, 30, 99, 100, 300};
        QCOMPARE(includedAudioRoots(processes, sessions, {"c:\\DISCORD\\discord.exe"}, 99), QList<quint32>({10, 30}));
        processes.append({12, 10, "C:/Discord/Discord.exe"});
        QCOMPARE(includedAudioRoots(processes, sessions, {"C:/Discord/Discord.exe"}, 99), QList<quint32>({11, 30}));
        QCOMPARE(includedAudioRoots(processes, {21, 100}, {"C:/Discord/Discord.exe"}, 99).size(), 0);
    }
    void replayCutsAtAKeyframeAndOrdersAllTracks() {
        ReplayBuffer buffer(3000000, 1048576);
        for (int second = 0; second < 8; ++second) {
            buffer.push({0, second * 1000000LL, second * 1000000LL, 1000000, second % 2 == 0, QByteArray(50, 'v')});
            buffer.push({1, second * 1000000LL - 50000, second * 1000000LL - 50000, 1000000, true, QByteArray(10, 'a')});
        }
        const auto snapshot = buffer.snapshot(); QCOMPARE(snapshot.startUs, 4000000); QCOMPARE(snapshot.endUs, 8000000);
        QVERIFY(snapshot.packets.first().stream == 0); QVERIFY(snapshot.packets.first().keyframe);
        qint64 previous = -1; for (const auto& packet : snapshot.packets) { QVERIFY(packet.dtsUs >= snapshot.startUs); QVERIFY(packet.dtsUs >= previous); previous = packet.dtsUs; }
    }
    void reorderedPicturesStartOnScreenTimeAndNeverEndOnAHole() {
        // Two B-frames between references, as an encoder hands them out: decoded two pictures late.
        ReplayBuffer buffer(60000000, 1048576); const qint64 frame = 10000;
        const auto push = [&](int shown, int decoded, bool key = false) { buffer.push({0, shown * frame, (decoded - 2) * frame, frame, key, QByteArray(20, 'v')}); };
        for (int index = 0; index < 8; ++index) buffer.push({1, index * frame, index * frame, frame, true, QByteArray(10, 'a')});
        push(0, 0, true); push(3, 1); push(1, 2); push(2, 3);
        // Nothing says yet that the pictures decoded after the first are all there: the clip stops on it.
        auto snapshot = buffer.snapshot(); QCOMPARE(snapshot.startUs, qint64(0)); QCOMPARE(snapshot.endUs, frame);
        push(6, 4); snapshot = buffer.snapshot(); QCOMPARE(snapshot.endUs, 4 * frame);
        push(4, 5); QCOMPARE(buffer.snapshot().endUs, 4 * frame);
        push(5, 6); push(9, 7); snapshot = buffer.snapshot(); QCOMPARE(snapshot.startUs, qint64(0)); QCOMPARE(snapshot.endUs, 7 * frame);
        int pictures = 0, sounds = 0; for (const auto& packet : snapshot.packets) { QVERIFY(packet.ptsUs >= 0 && packet.ptsUs < snapshot.endUs); packet.stream == 0 ? ++pictures : ++sounds; }
        QCOMPARE(pictures, 7); QCOMPARE(sounds, 7); QVERIFY(snapshot.packets.first().stream == 0 && snapshot.packets.first().keyframe);
    }
    void memoryLimitDropsWholeGroupsOfPictures() {
        EncodedPacket packet{0, 0, 0, 1000000, true, QByteArray(256, 'v')};
        ReplayBuffer buffer(60000000, packet.storageBytes() * 3);
        for (int second = 0; second < 8; ++second) { packet.ptsUs = packet.dtsUs = second * 1000000LL; packet.keyframe = second % 2 == 0; buffer.push(packet); QVERIFY(buffer.bytes() <= packet.storageBytes() * 3); }
        const auto snapshot = buffer.snapshot(); QVERIFY(!snapshot.packets.isEmpty()); QVERIFY(snapshot.packets.first().keyframe); QCOMPARE(snapshot.startUs, 6000000);
        const auto held = snapshot.packets.first().data;
        buffer.clear(); QCOMPARE(buffer.bytes(), 0); QCOMPARE(held.size(), 256);
        ReplayBuffer tooSmall(60000000, 64); tooSmall.push(packet); QVERIFY(tooSmall.snapshot().packets.isEmpty()); QCOMPARE(tooSmall.bytes(), 0);
    }
    void replayWaitsForAKeyframeAfterAnOverflow() {
        ReplayBuffer buffer(60000000, 1024);
        buffer.push({0, 0, 0, 1000, true, QByteArray(2000, 'v')});
        buffer.push({0, 1000, 1000, 1000, false, QByteArray(20, 'v')}); QVERIFY(buffer.snapshot().packets.isEmpty());
        buffer.push({0, 2000, 2000, 1000, true, QByteArray(20, 'v')}); const auto snapshot = buffer.snapshot(); QCOMPARE(snapshot.startUs, 2000); QCOMPARE(snapshot.packets.size(), 1);
    }
    void firstLaunchDoesNotWriteFiles() {
        QTemporaryDir directory; QVERIFY(directory.isValid()); ConfigStore store(directory.path()); AppConfig config; QString error;
        QVERIFY2(store.load(config, error), qPrintable(error)); QVERIFY(config.activeProfile()); QCOMPARE(config.activeProfile()->video.fps, 60);
        QVERIFY(!QFile::exists(store.filePath())); QVERIFY(config.validationErrors().isEmpty());
    }
    void sharingPreferencesAndDedicatedFilesAreSeparatedFromTheLibrary() {
        auto config = AppConfig::defaults(); config.clipSavedSound = false; config.clipSavedNotification = false; config.discordLimitBytes = 1000000000; config.discordCodec = "av1"; config.discordAutoResize = false;
        AppConfig loaded; QString error; QVERIFY(AppConfig::fromJson(config.toJson(), loaded, error)); QVERIFY(!loaded.clipSavedSound); QVERIFY(!loaded.clipSavedNotification); QCOMPARE(loaded.discordLimitBytes, qint64(1000000000)); QCOMPARE(loaded.discordCodec, QString("av1")); QVERIFY(!loaded.discordAutoResize);
        auto legacy = config.toJson(); legacy.remove("discord"); legacy.remove("clipSavedSound"); legacy.remove("clipSavedNotification"); QVERIFY(AppConfig::fromJson(legacy, loaded, error)); QVERIFY(loaded.clipSavedSound); QCOMPARE(loaded.discordLimitBytes, qint64(20000000));
        QTemporaryDir directory; QVERIFY(QDir(directory.path()).mkpath("Partage Discord"));
        for (const auto& name : {"source.mp4", "source-haute-qualité.mp4", "Partage Discord/source.discord.mp4"}) { QFile file(directory.filePath(QString::fromUtf8(name))); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("generated"); }
        const auto originals = scanLibrary(directory.path()); QCOMPARE(originals.clips.size(), 2); for (const auto& clip : originals.clips) QVERIFY(!clip.path.endsWith(".discord.mp4"));
        const auto sharing = scanLibrary(directory.filePath("Partage Discord"), true); QCOMPARE(sharing.clips.size(), 1); QVERIFY(sharing.clips.first().path.endsWith(".discord.mp4"));
    }
    void profilesAndGroupedTracksRoundTrip() {
        QTemporaryDir directory; ConfigStore store(directory.path()); AppConfig original = AppConfig::defaults(); QString error;
        auto& first = original.profiles.first(); first.name = QStringLiteral("Jeu — équipe 🎮");
        first.sources[1].targetId = QStringLiteral("C:/Jeux/Mon jeu/jeu.exe"); first.sources[1].enabled = true;
        first.sources[2].trackId = first.sources[1].trackId;
        auto second = first; second.id = newId(); second.name = "4K"; second.video.width = 3840; second.video.height = 2160; second.video.fps = 30;
        original.profiles.append(second); original.activeProfileId = second.id;
        QVERIFY2(store.save(original, error), qPrintable(error)); AppConfig loaded; QVERIFY2(store.load(loaded, error), qPrintable(error));
        QCOMPARE(QJsonDocument(original.toJson()).toJson(), QJsonDocument(loaded.toJson()).toJson());
        QCOMPARE(loaded.activeProfile()->video.fps, 30); QCOMPARE(loaded.profiles.first().sources[1].trackId, loaded.profiles.first().sources[2].trackId);
    }
    void invalidSavePreservesExistingConfiguration() {
        QTemporaryDir directory; ConfigStore store(directory.path()); auto config = AppConfig::defaults(); QString error;
        QVERIFY(store.save(config, error)); QFile file(store.filePath()); QVERIFY(file.open(QIODevice::ReadOnly)); const auto original = file.readAll(); file.close();
        for (const int rate : {30, 60, 120, 144}) { auto fast = config; fast.profiles.first().video.fps = rate; QVERIFY(fast.validationErrors().isEmpty()); }
        config.profiles.first().video.fps = 50; QVERIFY(!store.save(config, error)); QVERIFY(!error.isEmpty());
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), original);
    }
    void corruptionIsReportedWithoutOverwriting() {
        QTemporaryDir directory; ConfigStore store(directory.path()); QFile file(store.filePath()); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("{broken"); file.close();
        AppConfig config; QString error; QVERIFY(!store.load(config, error)); QVERIFY(!error.isEmpty());
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), QByteArray("{broken"));
    }
    void danglingAudioRoutingIsRejected() {
        auto config = AppConfig::defaults(); config.profiles.first().sources.first().trackId = "missing"; QVERIFY(!config.validationErrors().isEmpty());
        AppConfig loaded; QString error; QVERIFY(!AppConfig::fromJson(config.toJson(), loaded, error));
    }
    void unsupportedVersionAndDuplicateIdsAreRejected() {
        auto config = AppConfig::defaults(); auto object = config.toJson(); object.insert("schemaVersion", 99); AppConfig loaded; QString error;
        QVERIFY(!AppConfig::fromJson(object, loaded, error));
        config.profiles.append(config.profiles.first()); QVERIFY(!config.validationErrors().isEmpty());
    }
    void bufferEstimateCountsTracksOnce() {
        auto profile = AppConfig::defaults().profiles.first();
        for (auto& source : profile.sources) { source.enabled = true; source.trackId = profile.tracks.first().id; }
        const auto oneTrack = estimatedBufferMiB(profile);
        profile.sources.last().trackId = profile.tracks.last().id;
        QVERIFY(estimatedBufferMiB(profile) > oneTrack);
        const auto expected = (25000000.0 + 192000.0) * 60 / 8 / 1048576;
        QVERIFY(qAbs(oneTrack - expected) < 0.001);
    }
    void recordingQualityRoundTripsAndOlderFilesKeepATargetBitrate() {
        auto config = AppConfig::defaults(); auto& video = config.profiles.first().video;
        video.rateControl = "quality"; video.quality = 14; video.effort = "thorough"; video.bitrateMbps = 200;
        AppConfig loaded; QString error; QVERIFY2(AppConfig::fromJson(config.toJson(), loaded, error), qPrintable(error));
        QCOMPARE(loaded.profiles.first().video.rateControl, QString("quality")); QCOMPARE(loaded.profiles.first().video.quality, 14); QCOMPARE(loaded.profiles.first().video.effort, QString("thorough"));
        // A file written before these settings existed records as it always did.
        auto older = config.toJson(); auto profiles = older.value("profiles").toArray(); auto profile = profiles.first().toObject(); auto stored = profile.value("video").toObject();
        for (const auto& key : {"rateControl", "quality", "effort"}) stored.remove(QLatin1String(key));
        profile.insert("video", stored); profiles.replace(0, profile); older.insert("profiles", profiles);
        QVERIFY2(AppConfig::fromJson(older, loaded, error), qPrintable(error)); QCOMPARE(loaded.profiles.first().video.rateControl, QString("bitrate")); QCOMPARE(loaded.profiles.first().video.effort, QString("fast"));
        for (const auto& broken : {QStringLiteral("rateControl"), QStringLiteral("effort")}) { auto invalid = config; (broken == "effort" ? invalid.profiles.first().video.effort : invalid.profiles.first().video.rateControl) = "unknown"; QVERIFY(!invalid.validationErrors().isEmpty()); }
        video.quality = 5; QVERIFY(!config.validationErrors().isEmpty()); video.quality = 14; video.bitrateMbps = 301; QVERIFY(!config.validationErrors().isEmpty());
    }
    void constantQualityIsEstimatedAsARangeThatFollowsItsLevelUnderItsCeiling() {
        auto profile = AppConfig::defaults().profiles.first(); for (auto& source : profile.sources) source.enabled = false;
        // A target bitrate weighs what it says.
        auto usual = usualBufferMiB(profile); QCOMPARE(usual.low, usual.high); QCOMPARE(usual.high, estimatedBufferMiB(profile));
        auto& video = profile.video; video.width = 2560; video.height = 1440; video.fps = 60; video.rateControl = "quality"; video.quality = 20; video.bitrateMbps = 300;
        usual = usualBufferMiB(profile); QVERIFY(qAbs(usual.low - 56e6 * 60 / 8 / 1048576) < 0.5); QVERIFY(qAbs(usual.high - 91e6 * 60 / 8 / 1048576) < 0.5); QVERIFY(usual.high < estimatedBufferMiB(profile));
        // A better level weighs more, a smaller picture less, and nothing goes over the ceiling.
        video.quality = 14; const auto better = usualBufferMiB(profile); QVERIFY(better.low > usual.low * 2 && better.high > usual.high * 2);
        video.width = 1920; video.height = 1080; QVERIFY(usualBufferMiB(profile).high < better.high);
        video.bitrateMbps = 50; const auto capped = usualBufferMiB(profile); QCOMPARE(capped.low, estimatedBufferMiB(profile)); QCOMPARE(capped.high, capped.low);
    }
    void usualMixIsProposedOnlyWhenTheLastExportsAgree() {
        const auto micro = mixKey("Micro"), discord = mixKey(" DISCORD "), game = mixKey("Jeu"); QCOMPARE(discord, QString("discord"));
        const auto levels = [&](double m, double d, double g) { return TrackMix{{micro, m}, {discord, d}, {game, g}}; };
        QList<TrackMix> history; rememberExport(history, levels(0.7, 1.3, 1)); rememberExport(history, levels(0.72, 1.25, 1));
        // Two exports are not a habit yet.
        QVERIFY(!suggestedMix(history, {}, {}));
        rememberExport(history, levels(0.65, 1.3, 1));
        // The median, to the nearest five percent; a track left at its usual level stays out of it.
        auto proposal = suggestedMix(history, {}, {}); QVERIFY(proposal); QCOMPARE(*proposal, (TrackMix{{micro, 0.7}, {discord, 1.3}}));
        // Refused, it is not proposed again; a habit that moved on is.
        QVERIFY(!suggestedMix(history, {}, *proposal)); QVERIFY(suggestedMix(history, {}, TrackMix{{micro, 0.4}, {discord, 1.3}}));
        // Once it is the usual mix, exports that keep it ask nothing.
        QList<TrackMix> kept; for (int count = 0; count < 3; ++count) rememberExport(kept, *proposal); QVERIFY(!suggestedMix(kept, *proposal, {}));
        // Levels that go both ways, or far from one another, are choices made for each clip.
        QList<TrackMix> mixed{levels(0.7, 1, 1), levels(1.3, 1, 1), levels(0.7, 1, 1)}; QVERIFY(!suggestedMix(mixed, {}, {}));
        QList<TrackMix> apart{levels(0.9, 1, 1), levels(0.3, 1, 1), levels(0.6, 1, 1)}; QVERIFY(!suggestedMix(apart, {}, {}));
        // Only the last three count, and only a few are kept.
        for (int count = 0; count < 10; ++count) rememberExport(history, levels(1, 1, 0.8)); QCOMPARE(history.size(), 6);
        proposal = suggestedMix(history, {}, {}); QVERIFY(proposal); QCOMPARE(*proposal, (TrackMix{{game, 0.8}}));
        QCOMPARE(mixLevel(*proposal, "JEU"), 0.8); QCOMPARE(mixLevel(*proposal, "Micro"), 1.0); QVERIFY(sameMix(TrackMix{{micro, 1}}, {})); QVERIFY(!sameMix(TrackMix{{micro, 0.9}}, {}));
    }
    void usualMixRoundTripsAndOnlyReachesClipsThatWereNotMixedYet() {
        auto config = AppConfig::defaults(); config.defaultMix = {{mixKey("Micro"), 0.7}, {mixKey("Discord"), 1.3}}; config.mixDeclined = {{mixKey("Jeu"), 0.8}}; rememberExport(config.mixHistory, config.defaultMix);
        AppConfig loaded; QString error; QVERIFY2(AppConfig::fromJson(config.toJson(), loaded, error), qPrintable(error));
        QCOMPARE(loaded.defaultMix, config.defaultMix); QCOMPARE(loaded.mixDeclined, config.mixDeclined); QCOMPARE(loaded.mixHistory, config.mixHistory);
        // A level the editor could not show is dropped rather than refused with the whole file.
        auto json = config.toJson(); auto mix = json.value("mix").toObject(); mix.insert("levels", QJsonObject{{"Micro", 5}, {"Jeu", 0.5}}); json.insert("mix", mix);
        QVERIFY(AppConfig::fromJson(json, loaded, error)); QCOMPARE(loaded.defaultMix, (TrackMix{{mixKey("Jeu"), 0.5}}));
        ClipInfo info; info.size = {320, 192}; info.durationMs = 1000; info.audio = {{1, "Micro", "aac"}, {2, "Jeu", "aac"}, {3, "DISCORD", "aac"}};
        auto edit = defaultEdit(info, config.defaultMix); QCOMPARE(edit.tracks[0].volume, 0.7); QCOMPARE(edit.tracks[1].volume, 1.0); QCOMPARE(edit.tracks[2].volume, 1.3);
        edit.tracks[1].muted = true; QCOMPARE(editLevels(edit), config.defaultMix);
        info.mixed = true; for (const auto& track : defaultEdit(info, config.defaultMix).tracks) QCOMPARE(track.volume, 1.0);
    }
    void startMenuShortcutIsAddedRepairedAndRemovedWithoutTouchingItsNeighbours() {
        QTemporaryDir directory; const auto programs = directory.filePath("Programmes"), executable = QCoreApplication::applicationFilePath(); QString error;
        QVERIFY(!startMenuShortcutWorks(programs));
        QVERIFY2(setStartMenuShortcut(programs, executable, true, error), qPrintable(error)); QVERIFY(startMenuShortcutWorks(programs));
        QCOMPARE(QFileInfo(QFileInfo(startMenuShortcut(programs)).symLinkTarget()), QFileInfo(executable));
        QFile neighbour(QDir(programs).filePath("Autre programme.txt")); QVERIFY(neighbour.open(QIODevice::WriteOnly)); neighbour.close();
        // The folder of the application was moved: the shortcut leads nowhere until it is drawn again.
        const auto moved = directory.filePath("Clipsapp.exe"); { QFile file(moved); QVERIFY(file.open(QIODevice::WriteOnly)); }
        QVERIFY(setStartMenuShortcut(programs, moved, true, error)); QVERIFY(startMenuShortcutWorks(programs)); QVERIFY(QFile::remove(moved)); QVERIFY(!startMenuShortcutWorks(programs));
        QVERIFY(setStartMenuShortcut(programs, executable, true, error)); QVERIFY(startMenuShortcutWorks(programs));
        QVERIFY(setStartMenuShortcut(programs, executable, false, error)); QVERIFY(!QFileInfo(startMenuShortcut(programs)).isSymLink()); QVERIFY(!startMenuShortcutWorks(programs));
        QVERIFY(setStartMenuShortcut(programs, executable, false, error)); QVERIFY(neighbour.exists());
    }
    void libraryUsesGameFoldersAndFiltersExtensions() {
        QTemporaryDir directory; QDir root(directory.path()); QVERIFY(root.mkpath("Mon jeu"));
        for (const auto& name : {QStringLiteral("Mon jeu/clip.MKV"), QStringLiteral("bureau.mp4"), QStringLiteral("ignore.txt")}) { QFile file(root.filePath(name)); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("fixture"); }
        const auto scan = scanLibrary(directory.path()); QCOMPARE(scan.clips.size(), 2); QSet<QString> games;
        for (const auto& clip : scan.clips) games.insert(clip.game);
        QVERIFY(games.contains("Mon jeu")); QVERIFY(games.contains(QStringLiteral("Non classé")));
    }
    void updatesComeOnlyFromFinalReleasesOfTheProject() {
        QCOMPARE(latestReleaseUrl("ami/Clipsapp"), QUrl("https://api.github.com/repos/ami/Clipsapp/releases/latest")); QVERIFY(!latestReleaseUrl("").isValid()); QVERIFY(!latestReleaseUrl("ami").isValid()); QVERIFY(!latestReleaseUrl("a/b/c").isValid());
        QCOMPARE(versionOfTag("v0.8.1"), QVersionNumber(0, 8, 1)); QCOMPARE(versionOfTag("0.9"), QVersionNumber(0, 9)); QCOMPARE(versionOfTag("V1.0.0"), QVersionNumber(1, 0, 0)); QVERIFY(versionOfTag("0.8.1-beta").isNull()); QVERIFY(versionOfTag("latest").isNull());
        QVERIFY(newerVersion(versionOfTag("v0.10.0"), versionOfTag("0.9.9"))); QVERIFY(newerVersion(versionOfTag("0.8.1"), versionOfTag("0.8.0"))); QVERIFY(!newerVersion(versionOfTag("0.8.0"), versionOfTag("0.8.0")));
        QVERIFY(!newerVersion(versionOfTag("v0.8.0"), versionOfTag("0.8"))); QVERIFY(!newerVersion(versionOfTag("0.8"), versionOfTag("0.8.0"))); QVERIFY(!newerVersion(versionOfTag("0.7.9"), versionOfTag("0.8.0")));
        // What GitHub answers for a release with two archives: only the one for this system counts.
        const QByteArray answer = R"({"tag_name":"v0.8.1","name":"Clipsapp 0.8.1","body":"- Ralenti\n- Capture d'image","html_url":"https://github.com/ami/Clipsapp/releases/tag/v0.8.1","draft":false,"prerelease":false,"assets":[
            {"name":"Source.tar.gz","state":"uploaded","size":10,"browser_download_url":"https://github.com/ami/Clipsapp/releases/download/v0.8.1/Source.tar.gz"},
            {"name":"Clipsapp-windows-x64.zip","state":"uploaded","size":70123456,"digest":"sha256:ABCDEF0123","browser_download_url":"https://github.com/ami/Clipsapp/releases/download/v0.8.1/Clipsapp-windows-x64.zip"}]})";
        QString error; const auto info = parseRelease(answer, "-windows-x64.zip", error); QVERIFY2(info.has_value(), qPrintable(error));
        QCOMPARE(info->version, QVersionNumber(0, 8, 1)); QCOMPARE(info->title, QString("Clipsapp 0.8.1")); QVERIFY(info->notes.contains("Ralenti")); QCOMPARE(info->fileName, QString("Clipsapp-windows-x64.zip"));
        QCOMPARE(info->bytes, qint64(70123456)); QCOMPARE(info->sha256, QByteArray("abcdef0123")); QVERIFY(trustedDownload(info->download, "ami/Clipsapp"));
        // Another repository, another site or plain HTTP is never downloaded from.
        QVERIFY(!trustedDownload(info->download, "autre/Clipsapp")); QVERIFY(!trustedDownload(info->download, "")); QVERIFY(!trustedDownload(QUrl("http://github.com/ami/Clipsapp/releases/download/v0.8.1/a.zip"), "ami/Clipsapp"));
        QVERIFY(!trustedDownload(QUrl("https://github.com.exemple.net/ami/Clipsapp/releases/download/v0.8.1/a.zip"), "ami/Clipsapp")); QVERIFY(!trustedDownload(QUrl("https://github.com/ami/Clipsapp/archive/a.zip"), "ami/Clipsapp"));
        // Drafts, pre-releases, a tag that is no version, no archive for this system, an answer that is no JSON.
        auto altered = answer; altered.replace("\"prerelease\":false", "\"prerelease\":true"); QVERIFY(!parseRelease(altered, "-windows-x64.zip", error)); QVERIFY(!error.isEmpty());
        altered = answer; altered.replace("\"draft\":false", "\"draft\":true"); QVERIFY(!parseRelease(altered, "-windows-x64.zip", error));
        altered = answer; altered.replace("v0.8.1\",\"name", "nightly\",\"name"); QVERIFY(!parseRelease(altered, "-windows-x64.zip", error));
        QVERIFY(!parseRelease(answer, "-linux-x64.tar.gz", error)); QVERIFY(error.contains("0.8.1")); QVERIFY(!parseRelease("<html>", "-windows-x64.zip", error));
    }
    void updateReplacesTheFilesAndPutsEverythingBackWhenItCannot() {
        QTemporaryDir directory; const QDir install(directory.filePath("Clipsapp")), staged(directory.filePath("Clipsapp/.update/new"));
        const auto write = [](const QString& path, const QByteArray& content) { QDir().mkpath(QFileInfo(path).absolutePath()); QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(content) == content.size(); };
        const auto read = [](const QString& path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray("<absent>"); };
        // Version 1 as installed, with a file the user added and one that version 2 no longer ships.
        for (const auto& name : {"Clipsapp.exe", "Qt6Core.dll", "platforms/qwindows.dll", "ancien.dll"}) QVERIFY(write(install.filePath(name), QByteArray("v1 ") + name));
        QVERIFY(write(install.filePath("manifest.txt"), "Clipsapp.exe\nQt6Core.dll\nplatforms/qwindows.dll\nancien.dll\nmanifest.txt\n..\\..\\ailleurs.txt\nC:/Windows/notepad.exe\n")); QVERIFY(write(install.filePath("mes notes.txt"), "à moi"));
        QVERIFY(write(directory.filePath("ailleurs.txt"), "hors du dossier"));
        for (const auto& name : {"Clipsapp.exe", "Qt6Core.dll", "platforms/qwindows.dll", "tls/nouveau.dll"}) QVERIFY(write(staged.filePath(name), QByteArray("v2 ") + name));
        QVERIFY(write(staged.filePath("manifest.txt"), "Clipsapp.exe\nQt6Core.dll\nplatforms/qwindows.dll\ntls/nouveau.dll\nmanifest.txt\n"));
        QVERIFY(canUpdateInPlace(install.path())); QVERIFY(!canUpdateInPlace(directory.filePath("absent"))); QCOMPARE(stagedRoot(staged.path(), "Clipsapp.exe"), staged.absolutePath());
        // A file held open without sharing cannot be renamed: nothing changes, and the new version is still there to try again.
        QString error;
#ifdef Q_OS_WIN
        { QFile held(install.filePath("platforms/qwindows.dll")); QVERIFY(held.open(QIODevice::ReadWrite)); QVERIFY(!applyUpdate(staged.path(), install.path(), "Clipsapp.exe", error)); QVERIFY(error.contains("qwindows.dll")); }
        QCOMPARE(read(install.filePath("Clipsapp.exe")), QByteArray("v1 Clipsapp.exe")); QCOMPARE(read(install.filePath("Qt6Core.dll")), QByteArray("v1 Qt6Core.dll")); QCOMPARE(read(staged.filePath("Clipsapp.exe")), QByteArray("v2 Clipsapp.exe")); QVERIFY(!QFile::exists(install.filePath("tls/nouveau.dll")));
#endif
        QVERIFY2(applyUpdate(staged.path(), install.path(), "Clipsapp.exe", error), qPrintable(error));
        for (const auto& name : {"Clipsapp.exe", "Qt6Core.dll", "platforms/qwindows.dll", "tls/nouveau.dll"}) QCOMPARE(read(install.filePath(name)), QByteArray("v2 ") + name);
        // What version 1 shipped and version 2 does not is set aside; the user's own file and anything outside the folder stay.
        QVERIFY(!QFile::exists(install.filePath("ancien.dll"))); QCOMPARE(read(install.filePath("mes notes.txt")), QByteArray("à moi")); QCOMPARE(read(directory.filePath("ailleurs.txt")), QByteArray("hors du dossier"));
        QVERIFY(QDir(updateWorkDirectory(install.path())).exists()); clearUpdateLeftovers(install.path()); QVERIFY(!QDir(updateWorkDirectory(install.path())).exists());
        // An archive that wraps the application in one folder is accepted; one without the application is not.
        QVERIFY(write(directory.filePath("zip/Clipsapp-0.8.2/Clipsapp.exe"), "v3")); QCOMPARE(stagedRoot(directory.filePath("zip"), "Clipsapp.exe"), directory.filePath("zip/Clipsapp-0.8.2"));
        QVERIFY(write(directory.filePath("vide/lisezmoi.txt"), "rien")); QVERIFY(stagedRoot(directory.filePath("vide"), "Clipsapp.exe").isEmpty()); QVERIFY(!applyUpdate(directory.filePath("vide"), install.path(), "Clipsapp.exe", error));
        QCOMPARE(read(install.filePath("Clipsapp.exe")), QByteArray("v2 Clipsapp.exe"));
    }
    void startupSettingsAreOffAtFirstAndSurviveRestart() {
        auto config = AppConfig::defaults(); QVERIFY(!config.launchAtLogin); QCOMPARE(config.autoReplay, QString("off")); QVERIFY(config.replayGames.isEmpty());
        config.launchAtLogin = true; config.autoReplay = "games"; config.replayGames = {"D:/Games/Valorant/VALORANT.exe", "C:/Program Files/CS2/cs2.exe"};
        AppConfig loaded; QString error; QVERIFY2(AppConfig::fromJson(config.toJson(), loaded, error), qPrintable(error));
        QVERIFY(loaded.launchAtLogin); QCOMPARE(loaded.autoReplay, QString("games")); QCOMPARE(loaded.replayGames, config.replayGames);
        // A game written twice, or with Windows separators, is one game.
        auto json = config.toJson(); auto startup = json.value("startup").toObject(); startup.insert("games", QJsonArray{"C:\\Jeux\\jeu.exe", "C:/Jeux/jeu.exe", "  "}); json.insert("startup", startup);
        QVERIFY(AppConfig::fromJson(json, loaded, error)); QCOMPARE(loaded.replayGames, QStringList({"C:/Jeux/jeu.exe"}));
        // A file written before these settings existed starts nothing by itself.
        json.remove("startup"); QVERIFY(AppConfig::fromJson(json, loaded, error)); QVERIFY(!loaded.launchAtLogin); QCOMPARE(loaded.autoReplay, QString("off"));
        startup.insert("autoReplay", "toujours"); json.insert("startup", startup); QVERIFY(!AppConfig::fromJson(json, loaded, error));
    }
    void autoReplayStartsWithAGameStopsAfterItAndRespectsTheUser() {
        using Action = AutoReplay::Action; AutoReplay policy; const qint64 grace = AutoReplay::graceMs;
        // Off, nothing ever starts; at launch, the replay starts once.
        QCOMPARE(policy.launched(), Action::None); QCOMPARE(policy.tick(true, false, 0), Action::None);
        policy.setMode("launch"); QCOMPARE(policy.launched(), Action::Start); QCOMPARE(policy.tick(true, false, 0), Action::None);
        policy.setMode("games"); QCOMPARE(policy.launched(), Action::None);
        // A game opens: the replay starts, and runs as long as the game does.
        QCOMPARE(policy.tick(false, false, 0), Action::None); QCOMPARE(policy.tick(true, false, 1000), Action::Start); policy.started(true);
        QCOMPARE(policy.tick(true, true, 4000), Action::None);
        // Closed, the game leaves the replay running a little, so that its end can still be saved.
        QCOMPARE(policy.tick(false, true, 10000), Action::None); QCOMPARE(policy.tick(false, true, 10000 + grace - 1), Action::None);
        // Opened again meanwhile, nothing stops.
        QCOMPARE(policy.tick(true, true, 10000 + grace), Action::None); QCOMPARE(policy.tick(false, true, 50000), Action::None); QCOMPARE(policy.tick(false, true, 50000 + grace), Action::Stop);
        QVERIFY(!policy.automatic()); QCOMPARE(policy.tick(false, false, 50000 + grace + 3000), Action::None);
        // Stopped by the user while the game runs: it stays stopped until the game closes and opens again.
        QCOMPARE(policy.tick(true, false, 100000), Action::Start); policy.started(true); policy.stoppedByUser();
        QCOMPARE(policy.tick(true, false, 103000), Action::None); QVERIFY(policy.held());
        QCOMPARE(policy.tick(false, false, 106000), Action::None); QVERIFY(!policy.held()); QCOMPARE(policy.tick(true, false, 109000), Action::Start);
        // A replay that fails is not started again in a loop.
        policy.failed(); QCOMPARE(policy.tick(true, false, 112000), Action::None);
        // A replay the user started is the user's to stop, game or not.
        QCOMPARE(policy.tick(false, false, 115000), Action::None); policy.started(false);
        QCOMPARE(policy.tick(false, true, 118000), Action::None); QCOMPARE(policy.tick(false, true, 118000 + 2 * grace), Action::None);
        // Leaving the mode while an automatic replay runs leaves it running.
        policy.started(true); policy.setMode("off"); QCOMPARE(policy.tick(false, true, 300000), Action::None); QCOMPARE(policy.tick(false, true, 300000 + 2 * grace), Action::None);
    }
    void runningProgramsAreFoundByTheirFullPath() {
        const auto self = QCoreApplication::applicationFilePath();
        QCOMPARE(runningPrograms({self}), QStringList({self}));
        QCOMPARE(runningPrograms({QDir::toNativeSeparators(self).toUpper(), "C:/Absent/" + QFileInfo(self).fileName()}), QStringList({QDir::toNativeSeparators(self).toUpper()}));
        QVERIFY(runningPrograms({}).isEmpty());
    }
    void journalStaysSmallKeepsThePreviousFileAndFoldsRepeats() {
        QTemporaryDir directory; LogFile log(directory.filePath("logs"), 2000);
        log.write("INFO", "Clipsapp démarre."); for (int index = 0; index < 50; ++index) log.write("WARN", "Le même avertissement");
        log.write("INFO", "Ligne\nsur deux lignes");
        auto lines = log.tail(10); QCOMPARE(lines.size(), 5);
        QVERIFY(lines[0].endsWith("INFO   Clipsapp démarre.")); QVERIFY(lines[1].endsWith("Le même avertissement")); QVERIFY(lines[2].contains("répété 49 fois")); QVERIFY(lines[3].endsWith("Ligne")); QVERIFY(lines[4].trimmed() == "sur deux lignes");
        // Past its size, the file becomes the previous one; the one before that goes.
        for (int index = 0; index < 100; ++index) log.write("INFO", QStringLiteral("Message %1").arg(index));
        QVERIFY(QFileInfo(log.currentPath()).size() <= 2000); QVERIFY(QFileInfo(log.previousPath()).size() <= 2000); QCOMPARE(QDir(directory.filePath("logs")).entryList(QDir::Files).size(), 2);
        lines = log.tail(3); QCOMPARE(lines.size(), 3); QVERIFY(lines.last().endsWith("Message 99")); QVERIFY(lines.first().endsWith("Message 97"));
        // What is shared does not carry the name of the account.
        const auto home = QDir::homePath(); QCOMPARE(withoutHome(home + "/Videos/Clipsapp/a.mp4"), QString("~/Videos/Clipsapp/a.mp4")); QCOMPARE(withoutHome(QDir::toNativeSeparators(home) + "\\x"), QString("~\\x"));
    }
    void loginStartIsAddedRepairedAndRemoved() {
#ifdef Q_OS_WIN
        // A key of the tests' own, never the one Windows reads at the opening of the session.
        const QString location = "HKEY_CURRENT_USER\\Software\\Clipsapp-tests\\Run"; QString error;
        const auto cleanup = qScopeGuard([] { QSettings("HKEY_CURRENT_USER\\Software", QSettings::NativeFormat).remove("Clipsapp-tests"); });
        QSettings(location, QSettings::NativeFormat).setValue("Voisin", "C:/voisin.exe");
        QVERIFY(!loginStartWorks(location));
        QVERIFY2(setLoginStart(location, QCoreApplication::applicationFilePath(), true, error), qPrintable(error)); QVERIFY(loginStartWorks(location));
        QCOMPARE(QSettings(location, QSettings::NativeFormat).value("Clipsapp").toString(), loginStartCommand(QCoreApplication::applicationFilePath()));
        QVERIFY(loginStartCommand("C:/Mes jeux/Clipsapp/Clipsapp.exe") == QStringLiteral("\"C:\\Mes jeux\\Clipsapp\\Clipsapp.exe\" --autostart"));
        // A folder that moved leaves an entry that starts nothing.
        QVERIFY(setLoginStart(location, "C:/Absent/Clipsapp.exe", true, error)); QVERIFY(!loginStartWorks(location));
        QVERIFY(setLoginStart(location, QCoreApplication::applicationFilePath(), false, error)); QVERIFY(!loginStartWorks(location));
        QSettings run(location, QSettings::NativeFormat); QVERIFY(!run.contains("Clipsapp")); QCOMPARE(run.value("Voisin").toString(), QString("C:/voisin.exe"));
#else
        QSKIP("The session of Linux is checked on Linux.");
#endif
    }
    void uninstallRemovesOnlyThisCopysTracesAndNeverTheClips() {
#ifdef Q_OS_WIN
        QTemporaryDir directory; QString error;
        const auto install = directory.filePath("Clipsapp"), executable = install + "/Clipsapp.exe", other = directory.filePath("Autre copie/Clipsapp.exe");
        for (const auto& path : {executable, other}) { QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath())); QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("exe"); }
        const auto programs = directory.filePath("Programs"), config = directory.filePath("AppData/Clipsapp"); const QString location = "HKEY_CURRENT_USER\\Software\\Clipsapp-tests\\Run";
        const auto cleanup = qScopeGuard([] { QSettings("HKEY_CURRENT_USER\\Software", QSettings::NativeFormat).remove("Clipsapp-tests"); });
        auto settings = AppConfig::defaults(); const auto saveLibrary = [&](const QString& library) { settings.libraryPath = library; QString failure; return ConfigStore(config).save(settings, failure); };
        QVERIFY(setStartMenuShortcut(programs, executable, true, error)); QVERIFY(setLoginStart(location, executable, true, error));
        // Clips kept inside the folder about to be deleted: nothing is touched, and the script stops.
        QVERIFY(saveLibrary(install + "/Mes clips"));
        auto result = removeTraces({install, executable, config, programs, location, false});
        QCOMPARE(result.code, int(UninstallClipsInside)); QVERIFY(startMenuShortcutWorks(programs)); QVERIFY(loginStartWorks(location)); QVERIFY(QFile::exists(config + "/settings.json"));
        // Kept settings stay where they are; the shortcut and the session start go.
        QVERIFY(saveLibrary(directory.filePath("Vidéos/Clipsapp")));
        result = removeTraces({install, executable, config, programs, location, true});
        QCOMPARE(result.code, int(UninstallDone)); QVERIFY(!QFileInfo::exists(startMenuShortcut(programs))); QVERIFY(loginStartProgram(location).isEmpty()); QVERIFY(QFile::exists(config + "/settings.json"));
        // Those of another copy stay; settings not kept go, with the empty folder of the publisher above them.
        QVERIFY(setStartMenuShortcut(programs, other, true, error)); QVERIFY(setLoginStart(location, other, true, error));
        result = removeTraces({install, executable, config, programs, location, false});
        QCOMPARE(result.code, int(UninstallDone)); QVERIFY(startMenuShortcutWorks(programs)); QCOMPARE(loginStartProgram(location), QDir::toNativeSeparators(other));
        QVERIFY(!QFileInfo::exists(config)); QVERIFY(QFileInfo::exists(directory.filePath("AppData")));
        // Nothing left to remove is not a failure.
        QCOMPARE(removeTraces({install, executable, config, programs, location, false}).code, int(UninstallDone));
#else
        QSKIP("The uninstall script is a Windows one.");
#endif
    }
    void eachGameKeepsItsOwnLevelsOverTheUsualOnes() {
        const TrackMix general{{"micro", 1.2}, {"jeu", 0.9}}; const QMap<QString, TrackMix> games{{"ARC Raiders", TrackMix{{"jeu", 0.5}}}};
        const auto arc = mixForGame(general, games, "ARC Raiders"); QCOMPARE(arc.value("jeu"), 0.5); QCOMPARE(arc.value("micro"), 1.2);
        QCOMPARE(mixForGame(general, games, "EA SPORTS FC 27"), general); QCOMPARE(mixForGame(general, games, {}), general);
        // Saved and read again, with what each game learns from and what it refused.
        auto config = AppConfig::defaults(); config.gameMixes = games; config.gameMixHistory.insert("ARC Raiders", {TrackMix{{"jeu", 0.5}}, TrackMix{{"jeu", 0.45}}}); config.gameMixDeclined.insert("EA SPORTS FC 27", TrackMix{{"jeu", 0.7}});
        AppConfig loaded; QString error; QVERIFY2(AppConfig::fromJson(config.toJson(), loaded, error), qPrintable(error));
        QCOMPARE(loaded.gameMixes, config.gameMixes); QCOMPARE(loaded.gameMixHistory.value("ARC Raiders").size(), 2); QCOMPARE(loaded.gameMixDeclined.value("EA SPORTS FC 27").value("jeu"), 0.7);
        QVERIFY(!loaded.gameMixHistory.contains("EA SPORTS FC 27"));
    }
    void screenshotSettingsRoundTripAndNeverShareTheReplayShortcut() {
        auto config = AppConfig::defaults(); QCOMPARE(config.screenshotHotkey, QString("Ctrl+Alt+F11")); QVERIFY(config.screenshotSound); QCOMPARE(config.screenshotSoundId, QString("shutter"));
        config.screenshotHotkey = "Ctrl+Shift+S"; config.screenshotSound = false; config.screenshotSoundId = "chime"; config.screenshotSoundVolume = 35;
        AppConfig loaded; QString error; QVERIFY2(AppConfig::fromJson(config.toJson(), loaded, error), qPrintable(error));
        QCOMPARE(loaded.screenshotHotkey, QString("Ctrl+Shift+S")); QVERIFY(!loaded.screenshotSound); QCOMPARE(loaded.screenshotSoundId, QString("chime")); QCOMPARE(loaded.screenshotSoundVolume, 35);
        // One shortcut cannot save a clip and take a screenshot.
        config.screenshotHotkey = config.profiles.first().replay.hotkey; QVERIFY(!config.validationErrors().isEmpty());
        // A file from before screenshots, whose replay already uses their default, still loads: they take another.
        auto json = AppConfig::defaults().toJson(); json.remove("screenshots"); auto profiles = json.value("profiles").toArray(); auto profile = profiles.first().toObject(); auto replay = profile.value("replay").toObject();
        replay.insert("hotkey", "Ctrl+Alt+F11"); profile.insert("replay", replay); profiles.replace(0, profile); json.insert("profiles", profiles);
        QVERIFY2(AppConfig::fromJson(json, loaded, error), qPrintable(error)); QCOMPARE(loaded.screenshotHotkey, QString("Ctrl+Alt+F12"));
    }
    void screenshotsAreListedApartFromTheClips() {
        QTemporaryDir directory; const auto write = [&directory](const QString& name) { QDir().mkpath(QFileInfo(directory.filePath(name)).absolutePath()); QFile file(directory.filePath(name)); return file.open(QIODevice::WriteOnly) && file.write("x") == 1; };
        for (const auto& name : {"ARC Raiders/clip.mp4", "Screenshots/ARC Raiders/vue.png", "Screenshots/Photo.JPG", "Screenshots/ARC Raiders/notes.txt", "Screenshots/ARC Raiders/vidéo.mp4"}) QVERIFY(write(name));
        const auto clips = scanLibrary(directory.path()); QCOMPARE(clips.clips.size(), 1); QCOMPARE(clips.clips.first().name, QString("clip"));
        QCOMPARE(screenshotsDirectory(directory.path()), directory.filePath("Screenshots"));
        auto images = scanImages(screenshotsDirectory(directory.path())).clips; QCOMPARE(images.size(), 2);
        std::sort(images.begin(), images.end(), [](const auto& left, const auto& right) { return left.name < right.name; });
        QCOMPARE(images[0].name, QString("Photo")); QCOMPARE(images[0].game, QString("Non classé")); QCOMPARE(images[1].game, QString("ARC Raiders")); QCOMPARE(images[1].format, QString("PNG"));
        QVERIFY(isImageFile("a.webp")); QVERIFY(isImageFile("B.JPEG")); QVERIFY(!isImageFile("c.mp4"));
        const auto name = capturedFileName("ARC Raiders", "png"); QVERIFY(name.startsWith("ARC Raiders-20")); QVERIFY(name.endsWith(".png")); QVERIFY(name != capturedFileName("ARC Raiders", "png"));
    }
    void settingsComeUpOneFolderWithoutReplacingAnything() {
        QTemporaryDir directory; const auto config = directory.filePath("Clipsapp"), earlier = config + "/Clipsapp";
        const auto write = [](const QString& path, const QByteArray& content) { QDir().mkpath(QFileInfo(path).absolutePath()); QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(content) == content.size(); };
        const auto read = [](const QString& path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray("<absent>"); };
        QVERIFY(write(earlier + "/settings.json", "réglages")); QVERIFY(write(earlier + "/library-tags.json", "classement")); QVERIFY(write(earlier + "/logs/clipsapp.log", "journal"));
        adoptEarlierSettings(earlier, config);
        QCOMPARE(read(config + "/settings.json"), QByteArray("réglages")); QCOMPARE(read(config + "/library-tags.json"), QByteArray("classement")); QCOMPARE(read(config + "/logs/clipsapp.log"), QByteArray("journal"));
        QVERIFY(!QFileInfo::exists(earlier));
        // Something already in the new folder stays; what could not come up waits in the old one.
        QVERIFY(write(earlier + "/settings.json", "anciens")); QVERIFY(write(earlier + "/cache/index.json", "{}"));
        adoptEarlierSettings(earlier, config);
        QCOMPARE(read(config + "/settings.json"), QByteArray("réglages")); QCOMPARE(read(config + "/cache/index.json"), QByteArray("{}")); QCOMPARE(read(earlier + "/settings.json"), QByteArray("anciens"));
        // Nothing to bring, or the same folder: nothing happens.
        adoptEarlierSettings(directory.filePath("absent"), config); adoptEarlierSettings(config, config); QVERIFY(QFile::exists(config + "/settings.json"));
        QVERIFY(defaultConfigDirectory().endsWith("/Clipsapp")); QVERIFY(!defaultConfigDirectory().endsWith("Clipsapp/Clipsapp"));
    }
    void quickSyncGoesBackToAutomaticAndTheSavedSoundRoundTrips() {
        auto config = AppConfig::defaults(); QCOMPARE(config.clipSavedSoundId, QString("duo")); QCOMPARE(config.clipSavedSoundVolume, 50);
        config.clipSavedSoundId = "chime"; config.clipSavedSoundVolume = 80;
        auto json = config.toJson(); AppConfig loaded; QString error; QVERIFY2(AppConfig::fromJson(json, loaded, error), qPrintable(error));
        QCOMPARE(loaded.clipSavedSoundId, QString("chime")); QCOMPARE(loaded.clipSavedSoundVolume, 80);
        json.insert("clipSavedSoundVolume", 250); QVERIFY(AppConfig::fromJson(json, loaded, error)); QCOMPARE(loaded.clipSavedSoundVolume, 100);
        // A profile saved with Intel Quick Sync, offered once and never working, still loads.
        auto profiles = json.value("profiles").toArray(); auto profile = profiles.first().toObject(); auto video = profile.value("video").toObject();
        video.insert("encoder", "qsv"); profile.insert("video", video); profiles.replace(0, profile); json.insert("profiles", profiles);
        QVERIFY2(AppConfig::fromJson(json, loaded, error), qPrintable(error)); QCOMPARE(loaded.profiles.first().video.encoder, QString("auto"));
        config.profiles.first().video.encoder = "qsv"; QVERIFY(!config.validationErrors().isEmpty());
    }
    void anUpdateToTheRuntimeFolderLeavesOnlyTheExecutableAtTheRoot() {
        QTemporaryDir directory; const QDir install(directory.filePath("Clipsapp")), staged(directory.filePath("Clipsapp/.update/new"));
        const auto write = [](const QString& path, const QByteArray& content) { QDir().mkpath(QFileInfo(path).absolutePath()); QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(content) == content.size(); };
        // 0.8.2, as published: everything beside the executable, its list at the root.
        const QStringList flat{"Clipsapp.exe", "Qt6Core.dll", "platforms/qwindows.dll", "third_party/licenses/Qt-LGPL-3.0.txt", "README.md", "CHANGELOG.md"};
        for (const auto& name : flat) QVERIFY(write(install.filePath(name), "0.8.2"));
        QVERIFY(write(install.filePath("manifest.txt"), (flat.join('\n') + "\nmanifest.txt\n").toUtf8())); QVERIFY(write(install.filePath("mes notes.txt"), "à moi"));
        QVERIFY(publishedCopy(install.path()));
        const QStringList tidy{"Clipsapp.exe", "Desinstaller Clipsapp.bat", "runtime/Qt6Core.dll", "runtime/platforms/qwindows.dll", "runtime/licenses/Qt-LGPL-3.0.txt", "runtime/runtime.manifest"};
        for (const auto& name : tidy) QVERIFY(write(staged.filePath(name), "0.8.3"));
        QVERIFY(write(staged.filePath("runtime/manifest.txt"), (tidy.join('\n') + "\nruntime/manifest.txt\n").toUtf8()));
        QString error; QVERIFY2(applyUpdate(staged.path(), install.path(), "Clipsapp.exe", error), qPrintable(error));
        // Once the previous version is gone, the root keeps the executable, the script, "runtime", and what the user put there.
        clearUpdateLeftovers(install.path()); QVERIFY(!QFileInfo::exists(updateWorkDirectory(install.path())));
        QCOMPARE(install.entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase), QStringList({"Clipsapp.exe", "Desinstaller Clipsapp.bat", "mes notes.txt", "runtime"}));
        QVERIFY(publishedCopy(install.path())); QVERIFY(QFile::exists(install.filePath("runtime/platforms/qwindows.dll")));
        // An update made by 0.8.2 set the files aside but left their folders: they go with its leftovers,
        // unless something else is in them.
        QTemporaryDir other; const QDir earlier(other.path());
        QVERIFY(earlier.mkpath("platforms")); QVERIFY(write(earlier.filePath("tls/mon fichier.txt"), "à moi"));
        QVERIFY(write(earlier.filePath(".update/old-1/manifest.txt"), "platforms/qwindows.dll\ntls/qschannelbackend.dll\nmanifest.txt\n"));
        clearUpdateLeftovers(earlier.path());
        QVERIFY(!QFileInfo::exists(earlier.filePath("platforms"))); QVERIFY(QFile::exists(earlier.filePath("tls/mon fichier.txt"))); QVERIFY(!QFileInfo::exists(earlier.filePath(".update")));
    }
    void folderWatcherSeesTheWholeTreeWithoutHoldingIt() {
        QTemporaryDir directory; QVERIFY(QDir().mkpath(directory.filePath("Jeu")));
        FolderWatcher watcher; QSignalSpy changes(&watcher, &FolderWatcher::changed); watcher.watch(directory.path()); QVERIFY(watcher.watching());
        const auto touched = [&changes](const QString& path) { for (const auto& signal : changes) for (const auto& changed : signal.first().toStringList()) if (changed == path) return true; return false; };
        QFile clip(directory.filePath("Jeu/clip.mp4")); QVERIFY(clip.open(QIODevice::WriteOnly)); clip.write("clip"); clip.close();
        QTRY_VERIFY_WITH_TIMEOUT(touched("Jeu/clip.mp4"), 3000);
        // The folders stay the user's: they can be renamed and removed while they are followed.
        QVERIFY(QDir(directory.path()).rename("Jeu", "Autre jeu")); QTRY_VERIFY_WITH_TIMEOUT(touched("Autre jeu"), 3000);
        QVERIFY(QDir(directory.filePath("Autre jeu")).removeRecursively()); QVERIFY(!QFileInfo::exists(directory.filePath("Autre jeu")));
        // Asked again for the same folder, nothing restarts; another folder, or none, replaces it.
        watcher.watch(directory.path()); QVERIFY(watcher.watching());
        watcher.watch(QString()); QVERIFY(!watcher.watching()); watcher.watch(directory.filePath("absent")); QVERIFY(!watcher.watching());
        // A folder created since is followed when asked again.
        QVERIFY(QDir().mkpath(directory.filePath("absent"))); watcher.watch(directory.filePath("absent")); QVERIFY(watcher.watching());
    }
    void crashReportsAreAnnouncedOnce() {
        QTemporaryDir directory; QVERIFY(unseenCrashReport(directory.path()).isEmpty());
        QFile report(directory.filePath("crash-20261014-101500.dmp")); QVERIFY(report.open(QIODevice::WriteOnly)); report.write("MDMP"); report.close();
        QCOMPARE(unseenCrashReport(directory.path()), report.fileName()); QVERIFY(unseenCrashReport(directory.path()).isEmpty());
        QTest::qWait(20); QFile next(directory.filePath("crash-20261014-111500.dmp")); QVERIFY(next.open(QIODevice::WriteOnly)); next.write("MDMP"); next.close();
        QCOMPARE(unseenCrashReport(directory.path()), next.fileName());
    }
};
QTEST_GUILESS_MAIN(CoreTests)
#include "core_tests.moc"

