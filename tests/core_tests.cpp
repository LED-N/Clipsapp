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
};
QTEST_GUILESS_MAIN(CoreTests)
#include "core_tests.moc"

