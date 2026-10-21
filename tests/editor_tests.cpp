#include <QtTest>
#include <QAction>
#include <QCheckBox>
#include <QMenu>
#include <QSystemTrayIcon>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QSlider>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QListWidget>
#include <QPushButton>
#include <QAbstractButton>
#include <QAbstractScrollArea>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QUrl>
#include <QTemporaryDir>
#include <QScrollArea>
#include <QScrollBar>
#include <QProcess>
#include <QtConcurrent/QtConcurrentRun>
#include "capture/replay_engine.h"
#include "media/clip_reader.h"
#include "media/clip_player.h"
#include "media/clip_exporter.h"
#include "ui/clip_editor.h"
#include "ui/video_canvas.h"
#include "ui/clip_timeline.h"
#include "ui/source_dialog.h"
#include "ui/main_window.h"
#include "core/config_store.h"
#include "media/clip_analysis.h"
#include "ui/theme.h"
#include "ui/thumbnail_cache.h"
#include "ui/clip_list.h"
#include "platform/system_info.h"
#include "ui/library_page.h"
#include "ui/discord_batch.h"
#include "ui/discord_dialog.h"
#include "ui/widgets.h"
#include "ui/replay_strip.h"
#include "ui/clip_viewer.h"
#include "ui/layout_keys.h"
#include "ui/update_service.h"
#include "ui/settings_page.h"
#include "ui/clip_feedback.h"
#include "ui/video_surface.h"
#include "ui/screenshot_editor.h"
#include "ui/image_viewer.h"
#include "ui/whats_new.h"
#include <QPainter>
#include "ui/image_markup.h"
#include "platform/windows_video.h"
#include <QKeySequenceEdit>
#include <QClipboard>
#include <QThreadPool>
#include <QColorDialog>
#include <QStackedWidget>
#include <QtEndian>
#include <QToolTip>
#include <QWindow>
#include <QVBoxLayout>
#include <windows.h>
#include "core/update.h"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QShortcut>
#include <QWheelEvent>
#include "core/clip_library.h"
#include <cmath>
#include <memory>
namespace clipsapp { std::shared_ptr<CaptureBackend> createSyntheticCaptureBackend(const RecordingProfile&); }
using namespace clipsapp;
namespace {
double amplitude(const std::vector<float>& samples, double frequency) {
    double sine = 0, cosine = 0; const int frames = int(samples.size() / 2);
    for (int frame = 0; frame < frames; ++frame) {
        const double angle = 2 * 3.141592653589793 * frequency * frame / 48000;
        sine += samples[size_t(frame) * 2] * std::sin(angle); cosine += samples[size_t(frame) * 2] * std::cos(angle);
    }
    return 2 * std::sqrt(sine * sine + cosine * cosine) / frames;
}
QStringList clipPaths(const ClipListView* view) {
    QStringList paths;
    for (int row = 0; row < view->clips()->rowCount(); ++row) { const auto path = view->clips()->index(row, 0).data(ClipRole::Path).toString(); if (!path.isEmpty()) paths.append(path); }
    return paths;
}
// Screenshots of the interface under test, next to the test binary.
QString previewPath(const QString& name) { const QDir directory(QDir(QCoreApplication::applicationDirPath()).filePath("ui-previews")); directory.mkpath("."); return directory.filePath(name); }
QByteArray digest(const QString& path) { QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {}; QCryptographicHash hash(QCryptographicHash::Sha256); hash.addData(&file); return hash.result(); }
}
class EditorTests final : public QObject {
    Q_OBJECT
private:
    QTemporaryDir m_directory;
    QString m_source;
    ClipInfo m_info;
    QByteArray m_digest;
    std::atomic_bool m_stop{false};
    QString exported(ClipEdit edit, const QString& name, ClipExportOptions options = {}, QStringList encoders = {"libopenh264"}) {
        ClipExporter exporter; QSignalSpy done(&exporter, &ClipExporter::completed), failed(&exporter, &ClipExporter::failed);
        QString error; const auto path = m_directory.filePath(name);
        if (!exporter.start(m_info, edit, path, error, encoders, options)) { QTest::qFail(qPrintable(error), __FILE__, __LINE__); return {}; }
        QElapsedTimer timeout; timeout.start();
        while (done.isEmpty() && failed.isEmpty() && timeout.elapsed() < 15000) QTest::qWait(20);
        if (done.isEmpty()) { QTest::qFail(qPrintable(failed.isEmpty() ? QString("Export timed out") : failed.first().first().toString()), __FILE__, __LINE__); return {}; }
        return path;
    }
    std::vector<float> samples(ClipReader& reader, const QList<TrackEdit>& tracks) {
        const int count = int(std::min<qint64>(48000, (reader.info().durationMs - 500) * 48));
        reader.seek(500); reader.readUntil(500 + count / 48 + 100, m_stop); std::vector<float> result(size_t(count) * 2);
        reader.mix(24000, count, tracks, result.data()); return result;
    }
private slots:
    void initTestCase() {
        applyTheme(*qobject_cast<QApplication*>(QCoreApplication::instance())); QVERIFY(m_directory.isValid());
        auto profile = AppConfig::defaults().profiles.first(); profile.video.width = 320; profile.video.height = 192; profile.video.fps = 30; profile.video.bitrateMbps = 5;
        for (auto& source : profile.sources) source.enabled = true;
        const auto backend = createSyntheticCaptureBackend(profile); std::atomic_bool stop{false};
        auto future = QtConcurrent::run([&] { return backend->run(stop, [](QString) {}); });
        QElapsedTimer timeout; timeout.start();
        while (backend->stats().videoFrames < 90 && !future.isFinished() && timeout.elapsed() < 6000) QTest::qWait(20);
        m_source = m_directory.filePath(QStringLiteral("Original avec été.mp4")); const auto saved = backend->save(m_source);
        stop = true; future.waitForFinished(); QVERIFY2(saved.error.isEmpty(), qPrintable(saved.error)); QVERIFY2(future.result().isEmpty(), qPrintable(future.result()));
        ClipReader reader(m_source, false); m_info = reader.info(); m_digest = digest(m_source);
        QCOMPARE(m_info.size, QSize(320, 192)); QCOMPARE(m_info.audio.size(), 3);
    }
    void decodesSeeksAndMixesAllTracksWithLiveVolumeAndMute() {
        ClipReader reader(m_source, false); reader.readUntil(300, m_stop);
        const auto first = reader.frameAt(0); QVERIFY(!first.isNull()); QCOMPARE(first.size(), QSize(320, 192));
        auto edit = defaultEdit(m_info);
        auto mixed = samples(reader, edit.tracks);
        for (double frequency : {330, 440, 550}) QVERIFY(qAbs(amplitude(mixed, frequency) - 0.12) < 0.015);
        edit.tracks[0].volume = 0.5; edit.tracks[1].volume = 1.25; edit.tracks[2].muted = true;
        mixed = samples(reader, edit.tracks);
        QVERIFY(qAbs(amplitude(mixed, 330) - 0.06) < 0.01); QVERIFY(qAbs(amplitude(mixed, 440) - 0.15) < 0.01); QVERIFY(amplitude(mixed, 550) < 0.005);
        reader.seek(1700); reader.readUntil(1850, m_stop); QVERIFY(!reader.frameAt(1700).isNull());
    }
    void soundStaysContinuousWhenAFileStampsItsAudioPacketsUnevenly() {
        // Some recorders date each audio packet at its arrival, five milliseconds early or late,
        // while the sound inside is continuous. Played at those dates it would crackle.
        const auto uneven = m_directory.filePath("uneven.mp4"); QProcess process;
        process.start(QDir(QCoreApplication::applicationDirPath()).filePath("ffmpeg.exe"), {"-nostdin", "-v", "error", "-i", m_source, "-map", "0", "-c", "copy", "-bsf:a", "setts=pts=PTS+240*sin(N*1.7):dts=DTS+240*sin(N*1.7)", uneven});
        QVERIFY(process.waitForFinished(15000)); QVERIFY2(process.exitCode() == 0, process.readAllStandardError().constData());
        ClipReader reader(uneven, false); auto edit = defaultEdit(reader.info()); QCOMPARE(edit.tracks.size(), 3);
        const auto mixed = samples(reader, {edit.tracks.first()}); QVERIFY(qAbs(amplitude(mixed, 330) - 0.12) < 0.015);
        // A 330 Hz tone of this level never moves by more than 0.006 between two samples: a hole or a doubled block does.
        float jump = 0; for (size_t sample = 2; sample < mixed.size(); sample += 2) jump = std::max(jump, std::abs(mixed[sample] - mixed[sample - 2]));
        QVERIFY2(jump < 0.03f, qPrintable(QString::number(jump)));
    }
    void playbackPausesSeeksEndsAtTrimBoundaryAndReleasesTheClip() {
        ClipPlayer player(nullptr, false); player.load(m_source);
        QTRY_VERIFY_WITH_TIMEOUT(player.snapshot().loaded || !player.snapshot().error.isEmpty(), 5000);
        QVERIFY2(player.snapshot().error.isEmpty(), qPrintable(player.snapshot().error)); QVERIFY(!player.snapshot().image.isNull());
        player.setPlaying(true); QTRY_VERIFY_WITH_TIMEOUT(player.snapshot().positionMs > 200, 3000);
        player.setPlaying(false); QTRY_VERIFY_WITH_TIMEOUT(!player.snapshot().playing && !player.snapshot().loading, 3000);
        const auto paused = player.snapshot().positionMs; QTest::qWait(100); QCOMPARE(player.snapshot().positionMs, paused);
        player.seek(1700); QTRY_VERIFY_WITH_TIMEOUT(!player.snapshot().loading && player.snapshot().positionMs == 1700, 3000);
        auto edit = defaultEdit(m_info); edit.startMs = 500; edit.endMs = 2000; player.setEdit(edit); player.setPlaying(true);
        QTRY_VERIFY_WITH_TIMEOUT(!player.snapshot().playing && player.snapshot().positionMs == 2000, 3000);
        player.close(); QVERIFY(!player.snapshot().loaded); QVERIFY(player.snapshot().image.isNull()); QVERIFY(player.snapshot().info.path.isEmpty());
    }
    void exportsTrimCropVolumesAndTwoNamedSeparateTracks() {
        auto edit = defaultEdit(m_info); edit.crop = {80, 48, 160, 96}; edit.startMs = 400; edit.endMs = 1800; edit.separateTracks = true;
        edit.tracks[0].volume = 0.5; edit.tracks[1].volume = 1.25; edit.tracks[2].muted = true;
        const auto path = exported(edit, QStringLiteral("Recadré pistes séparées.mp4")); QVERIFY(!path.isEmpty());
        ClipReader reader(path, false); const auto info = reader.info(); QCOMPARE(info.size, QSize(160, 96)); QCOMPARE(info.audio.size(), 2);
        QCOMPARE(info.audio[0].name, QString("Micro")); QCOMPARE(info.audio[1].name, QString("Jeu")); QVERIFY(qAbs(info.durationMs - 1400) <= 50);
        const auto mixed = samples(reader, defaultEdit(info).tracks);
        QVERIFY(qAbs(amplitude(mixed, 330) - 0.06) < 0.012); QVERIFY(qAbs(amplitude(mixed, 440) - 0.15) < 0.012); QVERIFY(amplitude(mixed, 550) < 0.005);
        QCOMPARE(digest(m_source), m_digest);
    }
    void exportsOneMixedTrackOrNoAudioAndPreservesTheSource() {
        auto edit = defaultEdit(m_info); edit.endMs = 1600; edit.tracks[2].muted = true;
        const auto path = exported(edit, "mix.mp4"); QVERIFY(!path.isEmpty());
        ClipReader reader(path, false); QCOMPARE(reader.info().audio.size(), 1); QCOMPARE(reader.info().audio.first().name, QString("Mixage"));
        const auto mixed = samples(reader, defaultEdit(reader.info()).tracks);
        QVERIFY(qAbs(amplitude(mixed, 330) - 0.12) < 0.015); QVERIFY(qAbs(amplitude(mixed, 440) - 0.12) < 0.015); QVERIFY(amplitude(mixed, 550) < 0.005);
        for (auto& track : edit.tracks) track.muted = true;
        const auto silentPath = exported(edit, "silent.mp4"); QVERIFY(!silentPath.isEmpty()); ClipReader silent(silentPath, false); QVERIFY(silent.info().audio.isEmpty());
        ClipExporter exporter; QString error; QVERIFY(!exporter.start(m_info, edit, m_source, error)); QCOMPARE(digest(m_source), m_digest);
    }
    void removedTracksAreExcludedFromDecodingAnalysisAndExport() {
        auto edit = defaultEdit(m_info); edit.tracks.removeAt(1); edit.tracks.removeLast();
        ClipReader reader(m_source, false); QVERIFY(reader.setAudioTracks(edit.tracks));
        const auto kept = samples(reader, defaultEdit(m_info).tracks);
        QVERIFY(amplitude(kept, 330) > 0.1); QVERIFY(amplitude(kept, 440) < 0.002); QVERIFY(amplitude(kept, 550) < 0.002);
        QVERIFY(reader.setAudioTracks(defaultEdit(m_info).tracks)); const auto restored = samples(reader, defaultEdit(m_info).tracks); QVERIFY(amplitude(restored, 440) > 0.1);
        const auto analysis = analyzeClip(m_source, m_stop, QList<int>{edit.tracks.first().stream});
        QVERIFY2(analysis.error.isEmpty(), qPrintable(analysis.error)); QCOMPARE(analysis.peaks.size(), 1); QCOMPARE(analysis.streams, QList<int>{edit.tracks.first().stream}); QVERIFY(!analysis.thumbnails.isEmpty());
        edit.endMs = 1600; const auto path = exported(edit, "removed-tracks.mp4"); QVERIFY(!path.isEmpty());
        ClipReader output(path, false); QCOMPARE(output.info().audio.size(), 1); QCOMPARE(output.info().audio.first().name, QString("Micro"));
        const auto sound = samples(output, defaultEdit(output.info()).tracks); QVERIFY(amplitude(sound, 330) > 0.1); QVERIFY(amplitude(sound, 440) < 0.002);
        QCOMPARE(digest(m_source), m_digest);
    }
    void editorStartsFromTheUsualLevelsAndAnExportIsNotMixedTwice() {
        const TrackMix mix{{mixKey("Micro"), 0.7}, {mixKey("discord"), 1.3}};
        ClipEditor editor; editor.setAttribute(Qt::WA_DontShowOnScreen); editor.resize(1050, 740); editor.show(); editor.setDefaultMix(mix); editor.openClip(m_source);
        auto* micro = editor.findChild<QSlider*>("track-volume-0"); QTRY_VERIFY_WITH_TIMEOUT((micro = editor.findChild<QSlider*>("track-volume-0")) && micro->isEnabled(), 5000);
        // Micro, Jeu, Discord: each track opens at its usual level, a track without one as it was recorded.
        QCOMPARE(micro->value(), 70); QCOMPARE(editor.findChild<QSlider*>("track-volume-1")->value(), 100); QCOMPARE(editor.findChild<QSlider*>("track-volume-2")->value(), 130);
        // The usual levels can be exported, but they are not a change the user made to this clip.
        auto* exportButton = editor.findChild<QPushButton*>("export-clip"); auto* keep = editor.findChild<QPushButton*>("keep-mix"); auto* state = editor.findChild<QWidget*>("editor-state");
        QVERIFY(exportButton->isEnabled()); QVERIFY(state->isHidden()); QVERIFY(keep->isHidden());
        QSignalSpy chosen(&editor, &ClipEditor::mixChosen);
        editor.findChild<QSlider*>("track-volume-1")->setValue(85); QVERIFY(!state->isHidden()); QVERIFY(!keep->isHidden()); keep->click(); QCOMPARE(chosen.size(), 1);
        const auto kept = chosen.first().first().value<TrackMix>(); QCOMPARE(kept.value(mixKey("Jeu")), 0.85); QCOMPARE(kept.value(mixKey("Micro")), 0.7); QCOMPARE(kept.value(mixKey("Discord")), 1.3);
        editor.setDefaultMix(kept); QVERIFY(keep->isHidden()); QVERIFY(state->isHidden()); QVERIFY(exportButton->isEnabled());
        editor.suspend();
        // What an export writes says its sound was mixed: read again, it keeps the levels it has.
        QVERIFY(!m_info.mixed); auto edit = defaultEdit(m_info, mix); QCOMPARE(edit.tracks[0].volume, 0.7); edit.separateTracks = true;
        const auto output = exported(edit, "usual-levels.mp4"); QVERIFY(!output.isEmpty());
        ClipReader reader(output, false); QVERIFY(reader.info().mixed); QCOMPARE(reader.info().audio.size(), 3); QCOMPARE(reader.info().audio.first().name, QString("Micro"));
        for (const auto& track : defaultEdit(reader.info(), mix).tracks) QCOMPARE(track.volume, 1.0);
    }
    void editorOnlyExportsChangesAndAdaptsToZeroOneOrManyAudioTracks() {
        ClipEditor editor; editor.setAttribute(Qt::WA_DontShowOnScreen); editor.resize(1050, 740); editor.show(); editor.openClip(m_source);
        auto* width = editor.findChild<QSpinBox*>("crop-width"); QTRY_VERIFY_WITH_TIMEOUT(width->isEnabled(), 5000);
        auto* exportButton = editor.findChild<QPushButton*>("export-clip"); auto* mode = editor.findChild<Segmented*>("export-audio-mode"); QVERIFY(exportButton); QVERIFY(mode);
        QVERIFY(!exportButton->isEnabled()); QVERIFY(mode->isVisible());
        width->setValue(160); QVERIFY(exportButton->isEnabled()); width->setValue(320); QVERIFY(!exportButton->isEnabled());
        mode->setCurrentIndex(1); QVERIFY(exportButton->isEnabled()); mode->setCurrentIndex(0); QVERIFY(!exportButton->isEnabled());
        auto* volume = editor.findChild<QSlider*>("track-volume-0"); volume->setValue(50); QVERIFY(exportButton->isEnabled()); volume->setValue(100); QVERIFY(!exportButton->isEnabled());
        auto* timeline = editor.findChild<ClipTimeline*>();
        const auto remove = [&](int row) { auto* button = timeline->findChild<QAbstractButton*>(QString("track-remove-%1").arg(row)); QVERIFY(button); button->click(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); };
        remove(1); QCOMPARE(timeline->volumes().size(), 2); QVERIFY(mode->isVisible()); QVERIFY(exportButton->isEnabled());
        remove(1); QCOMPARE(timeline->volumes().size(), 1); QVERIFY(mode->isHidden());
        remove(0); QCOMPARE(timeline->volumes().size(), 0); QVERIFY(mode->isHidden()); QCOMPARE(editor.findChild<QLabel*>("export-audio-summary")->text(), QStringLiteral("Export sans audio"));
        editor.findChild<QPushButton*>("restore-audio-tracks")->click(); QCOMPARE(timeline->volumes().size(), 3); QVERIFY(mode->isVisible()); QVERIFY(!exportButton->isEnabled());
        editor.suspend();
    }
    void editorOffersTheOriginalForTrashOnlyAfterASuccessfulExportAndDisablesDuplicateExports() {
        ClipEditor editor; editor.setAttribute(Qt::WA_DontShowOnScreen); editor.show(); editor.openClip(m_source);
        auto* width = editor.findChild<QSpinBox*>("crop-width"); QTRY_VERIFY_WITH_TIMEOUT(width->isEnabled(), 5000);
        auto* exportButton = editor.findChild<QPushButton*>("export-clip"); auto* banner = editor.findChild<QWidget*>("export-banner"); QVERIFY(exportButton); QVERIFY(banner); QVERIFY(banner->isHidden());
        // The reason an export is unavailable is written next to the button.
        QVERIFY(static_cast<ElideLabel*>(editor.findChild<QLabel*>("editor-hint"))->fullText().startsWith(QStringLiteral("Rien à exporter")));
        QString error; const auto unchanged = m_directory.filePath("ui-unchanged.mp4"); QVERIFY(!editor.exportTo(unchanged, error)); QVERIFY(!QFile::exists(unchanged));
        auto* exporter = editor.findChild<ClipExporter*>(); QSignalSpy done(exporter, &ClipExporter::completed), deleteRequested(&editor, &ClipEditor::deleteRequested), opened(&editor, &ClipEditor::openRequested);
        const auto path = m_directory.filePath("ui-edited.mp4");
        width->setValue(160); QVERIFY(static_cast<Tag*>(editor.findChild<QWidget*>("editor-state"))->text() == QStringLiteral("Modifié")); QVERIFY2(editor.exportTo(path, error), qPrintable(error));
        QVERIFY(!exportButton->isVisible()); QVERIFY(editor.findChild<QPushButton*>("cancel-export")->isVisible());
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 10000); QVERIFY(QFile::exists(path)); QVERIFY(!banner->isHidden());
        // Nothing is removed without an explicit click, and the click itself only asks.
        QVERIFY(deleteRequested.isEmpty()); QCOMPARE(digest(m_source), m_digest);
        QVERIFY(!exportButton->isEnabled()); QVERIFY(!editor.exportTo(m_directory.filePath("ui-duplicate.mp4"), error)); QCOMPARE(digest(m_source), m_digest);
        width->setValue(320); QVERIFY(!exportButton->isEnabled());
        width->setValue(158); QVERIFY(exportButton->isEnabled());
        QVERIFY2(editor.exportTo(m_directory.filePath("ui-edited-again.mp4"), error), qPrintable(error)); QVERIFY(banner->isHidden());
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 2, 10000); QVERIFY(!banner->isHidden());
        editor.findChild<QPushButton*>("open-exported")->click(); QCOMPARE(opened.size(), 1); QCOMPARE(opened.first().first().toString(), m_directory.filePath("ui-edited-again.mp4"));
        editor.findChild<QPushButton*>("trash-original")->click();
        QCOMPARE(deleteRequested.size(), 1); QCOMPARE(deleteRequested.first().first().toString(), m_source); QVERIFY(!deleteRequested.first()[1].toBool()); QCOMPARE(digest(m_source), m_digest); editor.suspend();
    }
    void editorTrimsAtThePlayheadAndStepsFrameByFrame() {
        ClipEditor editor; editor.setAttribute(Qt::WA_DontShowOnScreen); editor.resize(1220, 760); editor.show(); editor.openClip(m_source);
        auto* start = editor.findChild<QDoubleSpinBox*>("trim-start"); auto* end = editor.findChild<QDoubleSpinBox*>("trim-end"); QTRY_VERIFY_WITH_TIMEOUT(start->isEnabled(), 5000);
        auto* player = editor.findChild<ClipPlayer*>(); QCOMPARE(start->text(), QStringLiteral("00:00.000"));
        player->seek(1000); QTRY_VERIFY_WITH_TIMEOUT(!player->snapshot().loading && player->snapshot().positionMs == 1000, 3000);
        editor.findChild<QPushButton*>("trim-start-here")->click(); QCOMPARE(start->value(), 1.0); QCOMPARE(start->text(), QStringLiteral("00:01.000"));
        const auto frame = qRound64(1000.0 / m_info.fps);
        editor.findChild<QPushButton*>("frame-next")->click(); QTRY_VERIFY_WITH_TIMEOUT(!player->snapshot().loading && player->snapshot().positionMs == 1000 + frame, 3000);
        // A step never leaves the selection.
        editor.findChild<QPushButton*>("frame-previous")->click(); editor.findChild<QPushButton*>("frame-previous")->click(); QTRY_VERIFY_WITH_TIMEOUT(!player->snapshot().loading && player->snapshot().positionMs == 1000, 3000);
        // The same steps from the keyboard: the two keys named on the buttons, whatever the layout.
        const auto keys = stepKeys(); QCOMPARE(editor.findChild<QPushButton*>("frame-next")->toolTip(), QStringLiteral("Image suivante (%1)").arg(keys.forwardLabel)); QCOMPARE(editor.findChild<QPushButton*>("frame-previous")->toolTip(), QStringLiteral("Image précédente (%1)").arg(keys.backLabel));
        const auto press = [&](Qt::Key key) { int found = 0; for (auto* shortcut : editor.findChildren<QShortcut*>()) if (shortcut->key() == QKeySequence(key)) { ++found; emit shortcut->activated(); } return found; };
        QCOMPARE(press(keys.forward), 1); QTRY_VERIFY_WITH_TIMEOUT(!player->snapshot().loading && player->snapshot().positionMs == 1000 + frame, 3000);
        QCOMPARE(press(keys.back), 1); QTRY_VERIFY_WITH_TIMEOUT(!player->snapshot().loading && player->snapshot().positionMs == 1000, 3000);
        QCOMPARE(press(Qt::Key_Comma), 0); QCOMPARE(press(Qt::Key_Semicolon), 0);
        player->seek(2000); QTRY_VERIFY_WITH_TIMEOUT(!player->snapshot().loading && player->snapshot().positionMs == 2000, 3000);
        editor.findChild<QPushButton*>("trim-end-here")->click(); QCOMPARE(end->value(), 2.0); QCOMPARE(editor.currentEdit().startMs, qint64(1000)); QCOMPARE(editor.currentEdit().endMs, qint64(2000));
        // Ratio tiles fill in the crop that already exists; "Entier" gives the whole image back.
        editor.findChild<QPushButton*>("crop-ratio-3")->click(); QCOMPARE(editor.currentEdit().crop, QRect(64, 0, 192, 192));
        editor.findChild<QPushButton*>("crop-ratio-0")->click(); QCOMPARE(editor.currentEdit().crop, QRect(QPoint(), m_info.size));
        QVERIFY(editor.grab().save(previewPath("editor-trimmed.png"))); editor.suspend();
    }
    void cancellationRemovesTemporaryOutputAndReportsIt() {
        ClipExporter exporter; QSignalSpy cancelled(&exporter, &ClipExporter::cancelled);
        QString error; const auto path = m_directory.filePath("cancelled.mp4");
        QVERIFY2(exporter.start(m_info, defaultEdit(m_info), path, error, {"libopenh264"}), qPrintable(error)); exporter.cancel();
        QTRY_COMPARE_WITH_TIMEOUT(cancelled.size(), 1, 3000); QVERIFY(!QFile::exists(path));
        QVERIFY(QDir(m_directory.path()).entryList({"*.part"}, QDir::Files | QDir::Hidden).isEmpty());
    }
    void sizedExportsRespectTheLimitMixAudioKeepFpsAndSupportAv1TwoPasses() {
        auto edit = defaultEdit(m_info); edit.startMs = 400; edit.endMs = 2400; edit.crop = {80, 48, 160, 96}; edit.tracks[0].volume = 0.5; edit.tracks[2].muted = true;
        ClipExportOptions options; options.maxBytes = 75000; options.tryCopyVideo = false; options.autoResize = false;
        for (const auto& codec : {QString("h264"), QString("av1")}) {
            options.codec = codec; const auto path = exported(edit, "sized-" + codec + ".discord.mp4", options, codec == "av1" ? QStringList{"libsvtav1"} : QStringList{"libopenh264"}); QVERIFY(!path.isEmpty()); QVERIFY(QFileInfo(path).size() <= options.maxBytes);
            ClipReader reader(path, false); QCOMPARE(reader.info().size, QSize(160, 96)); QVERIFY(qAbs(reader.info().durationMs - 2000) <= 60); QVERIFY(qAbs(reader.info().fps - 30) < 0.1); QCOMPARE(reader.info().audio.size(), 1); QCOMPARE(reader.info().videoCodec, codec);
            const auto audio = samples(reader, defaultEdit(reader.info()).tracks); QVERIFY(qAbs(amplitude(audio, 330) - 0.06) < 0.018); QVERIFY(qAbs(amplitude(audio, 440) - 0.12) < 0.018); QVERIFY(amplitude(audio, 550) < 0.005);
        }
        QCOMPARE(digest(m_source), m_digest); QVERIFY(QDir(m_directory.path()).entryList({".clipsapp-pass-*"}, QDir::Dirs | QDir::Hidden).isEmpty());
    }
    void underLimitVideoIsRemuxedWithoutQualityLossAndSizedCancellationCleansFiles() {
        ClipExportOptions options; options.maxBytes = 2000000;
        const auto path = exported(defaultEdit(m_info), "already-small.discord.mp4", options); QVERIFY(!path.isEmpty());
        const auto packetHash = [](const QString& file, bool audio = false) {
            QProcess process; process.start(QDir(QCoreApplication::applicationDirPath()).filePath("ffmpeg.exe"), {"-nostdin", "-v", "error", "-i", file, "-map", audio ? "0:a:0" : "0:v:0", "-c", "copy", "-f", "hash", "-hash", "sha256", "pipe:1"});
            if (!process.waitForFinished(5000) || process.exitCode()) return QByteArray{}; return process.readAllStandardOutput();
        };
        const auto originalHash = packetHash(m_source); QVERIFY(!originalHash.isEmpty()); QCOMPARE(packetHash(path), originalHash);
        ClipReader single(path, false, true); const auto singleInfo = single.info(); ClipExporter copy; QSignalSpy copied(&copy, &ClipExporter::completed); QString copyError; const auto copiedPath = m_directory.filePath("small-again.discord.mp4");
        QVERIFY2(copy.start(singleInfo, defaultEdit(singleInfo), copiedPath, copyError, {"libopenh264"}, options), qPrintable(copyError)); QTRY_COMPARE_WITH_TIMEOUT(copied.size(), 1, 5000); QCOMPARE(packetHash(copiedPath), originalHash); QCOMPARE(packetHash(copiedPath, true), packetHash(path, true));
        ClipExporter exporter; QSignalSpy cancelled(&exporter, &ClipExporter::cancelled); QString error; options.maxBytes = 75000; options.codec = "av1"; options.tryCopyVideo = false;
        const auto cancelledPath = m_directory.filePath("sized-cancelled.discord.mp4"); QVERIFY2(exporter.start(m_info, defaultEdit(m_info), cancelledPath, error, {"libsvtav1"}, options), qPrintable(error)); exporter.cancel();
        QTRY_COMPARE_WITH_TIMEOUT(cancelled.size(), 1, 3000); QVERIFY(!QFile::exists(cancelledPath)); QVERIFY(QDir(m_directory.path()).entryList({".clipsapp-pass-*"}, QDir::Dirs | QDir::Hidden).isEmpty()); QCOMPARE(digest(m_source), m_digest);
    }
    void timelineZoomAndPanKeepCoordinatesPreciseWithoutChangingTheClip() {
        ClipTimeline timeline; timeline.setAttribute(Qt::WA_DontShowOnScreen); timeline.resize(816, 250); timeline.setClip(m_info, defaultEdit(m_info)); timeline.show(); QSignalSpy sought(&timeline, &ClipTimeline::seekRequested);
        const auto span = qRound64(m_info.durationMs / 4.0);
        timeline.setZoom(4, 1500); QCOMPARE(timeline.visibleDuration(), span); QCOMPARE(timeline.viewStart(), qRound64(1500 - span / 2.0));
        QTest::mouseClick(&timeline, Qt::LeftButton, Qt::NoModifier, {511, 105}); QVERIFY(qAbs(sought.last().first().toLongLong() - 1500) <= 1);
        timeline.setViewStart(90000); QCOMPARE(timeline.viewStart(), m_info.durationMs - timeline.visibleDuration()); timeline.setZoom(128, 1500); QCOMPARE(timeline.grab().width(), 816); timeline.setZoom(1); QCOMPARE(timeline.viewStart(), qint64(0));
        ClipEditor editor; editor.setAttribute(Qt::WA_DontShowOnScreen); editor.show(); editor.openClip(m_source); auto* zoom = editor.findChild<QSlider*>("timeline-zoom"); QTRY_VERIFY_WITH_TIMEOUT(zoom->isEnabled(), 5000); zoom->setValue(4); QCOMPARE(editor.findChild<ClipTimeline*>()->zoomFactor(), 16.0); QVERIFY(!editor.findChild<QPushButton*>("export-clip")->isEnabled()); editor.suspend();
    }
    void nativeFileDragOffersOnlyVisibleAvailableFilesWithoutModelDataOrSourceChanges() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto first = directory.filePath(QStringLiteral("Clip été #1.mp4"));
        const auto hidden = directory.filePath("hidden.mp4");
        const auto last = directory.filePath(QStringLiteral("Deuxième clip + son.mp4"));
        for (const auto& path : {first, hidden, last}) QVERIFY(QFile::copy(m_source, path));
        ClipListView list;
        const QStringList paths{first, first, hidden, directory.filePath("missing.mp4"), directory.path(), last};
        auto* header = new QStandardItem; header->setFlags(Qt::NoItemFlags); header->setData(true, ClipRole::Header); header->setData(first, ClipRole::Path); list.clips()->appendRow(header);
        for (const auto& path : paths) {
            auto* row = new QStandardItem(QStringLiteral("The displayed name is not a file path")); row->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
            row->setData("not-a-path|123|456", ClipRole::Key); row->setData(path, ClipRole::Path); list.clips()->appendRow(row);
        }
        // A date heading can never be selected or dragged; a hidden row is not part of the drag either.
        list.selectAll(); list.setRowHidden(3, true); QCOMPARE(list.selectedPaths(), (QStringList{first, first, directory.filePath("missing.mp4"), directory.path(), last}));
        std::unique_ptr<QMimeData> payload(list.dragPayload()); QVERIFY(payload);
        QCOMPARE(payload->urls(), (QList<QUrl>{QUrl::fromLocalFile(first), QUrl::fromLocalFile(last)}));
        QCOMPARE(payload->formats(), QStringList{"text/uri-list"}); QVERIFY(payload->data("text/uri-list").size() < 4096);
        QVERIFY(!payload->hasFormat("application/x-qabstractitemmodeldatalist")); QVERIFY(!payload->hasFormat("video/mp4"));
        std::unique_ptr<QMimeData> fromModel(list.model()->mimeData(list.selectionModel()->selectedIndexes())); QVERIFY(fromModel); QCOMPARE(fromModel->formats(), QStringList{"text/uri-list"});
        QCOMPARE(list.model()->supportedDragActions(), Qt::DropActions(Qt::CopyAction)); QCOMPARE(list.defaultDropAction(), Qt::CopyAction); QVERIFY(!list.acceptDrops());
        list.selectPaths({directory.filePath("missing.mp4")}); std::unique_ptr<QMimeData> missing(list.dragPayload()); QVERIFY(!missing);
        for (const auto& path : {first, hidden, last}) QCOMPARE(digest(path), m_digest);
        QCOMPARE(list.clips()->rowCount(), int(paths.size()) + 1);
    }
    void editorSharingSavesHighQualityAndPlacesOnlySharingCopiesInTheirDedicatedPage() {
        QTemporaryDir directory; const auto original = directory.filePath("source.mp4"); QVERIFY(QFile::copy(m_source, original));
        ConfigStore store(directory.filePath("config")); auto config = AppConfig::defaults(); config.libraryPath = directory.path(); config.clipSavedSound = false; config.clipSavedNotification = false; QString error; QVERIFY(store.save(config, error));
        MainWindow window(directory.filePath("config")); window.setAttribute(Qt::WA_DontShowOnScreen); window.show(); auto* library = window.findChild<ClipListView*>("clip-library"); QTRY_COMPARE_WITH_TIMEOUT(clipPaths(library), QStringList{original}, 3000); emit library->openRequested(original);
        auto* editor = window.findChild<ClipEditor*>(); auto* width = editor->findChild<QSpinBox*>("crop-width"); QTRY_VERIFY_WITH_TIMEOUT(width->isEnabled(), 5000); width->setValue(160); editor->findChild<QDoubleSpinBox*>("trim-start")->setValue(0.4); editor->findChild<QDoubleSpinBox*>("trim-end")->setValue(2.4); editor->findChild<QSlider*>("track-volume-0")->setValue(50); editor->findChild<QAbstractButton*>("track-mute-2")->setChecked(true);
        editor->findChild<QPushButton*>("compress-discord-editor")->click(); auto* dialog = window.findChild<DiscordDialog*>(); auto* batch = window.findChild<DiscordBatch*>(); QVERIFY(dialog); QVERIFY(batch);
        QSignalSpy ready(batch, &DiscordBatch::shareReady), hq(batch, &DiscordBatch::highQualityReady); dialog->findChild<QDoubleSpinBox*>("discord-limit-mb")->setValue(1);
        QVERIFY(dialog->grab().save(previewPath("discord-options.png"))); dialog->findChild<QPushButton*>("discord-start")->click();
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 15000); QCOMPARE(hq.size(), 1); QTRY_VERIFY_WITH_TIMEOUT(!batch->running(), 3000); const auto share = ready.first().first().toString(); const auto quality = hq.first().first().toString();
        QVERIFY(QFileInfo(share).size() <= 1000000); QCOMPARE(digest(original), m_digest);
        for (const auto& path : {share, quality}) { ClipReader reader(path, false); QCOMPARE(reader.info().size.width(), 160); QVERIFY(qAbs(reader.info().durationMs - 2000) <= 60); QCOMPARE(reader.info().audio.size(), 1); const auto audio = samples(reader, defaultEdit(reader.info()).tracks); QVERIFY(qAbs(amplitude(audio, 330) - 0.06) < 0.018); QVERIFY(amplitude(audio, 550) < 0.005); }
        auto* sharing = window.findChild<ClipListView*>("discord-library"); QTRY_COMPARE_WITH_TIMEOUT(clipPaths(library).size(), 2, 3000); QTRY_COMPARE_WITH_TIMEOUT(clipPaths(sharing), QStringList{share}, 3000);
        // The copy remembers the clip it comes from and the limit it was made for; the clip says a copy is ready.
        LibraryLabels labels(directory.filePath("config/library-tags.json")); QVERIFY(labels.load(error)); QCOMPARE(labels.shareLink(share).source, QFileInfo(original).absoluteFilePath()); QCOMPARE(labels.shareLink(share).limitBytes, qint64(1000000));
        const auto shareIndex = sharing->clips()->index(sharing->rowOf(share), 0); QCOMPARE(shareIndex.data(ClipRole::ThumbPath).toString(), QFileInfo(original).absoluteFilePath()); QCOMPARE(shareIndex.data(ClipRole::Limit).toLongLong(), qint64(1000000));
        // A clip over the limit points to its ready copy; one that already fits needs none.
        QTRY_COMPARE_WITH_TIMEOUT(library->clips()->index(library->rowOf(original), 0).data(ClipRole::FitText).toString(), QFileInfo(original).size() > 1000000 ? QStringLiteral("Copie Discord prête") : QStringLiteral("Prêt pour Discord"), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(library->clips()->index(library->rowOf(quality), 0).data(ClipRole::FitText).toString(), QFileInfo(quality).size() > 1000000 ? QStringLiteral("Plus de 1 Mo") : QStringLiteral("Prêt pour Discord"), 3000);
        library->selectAll(); std::unique_ptr<QMimeData> originals(library->dragPayload()); QVERIFY(originals);
        QCOMPARE(originals->urls().size(), 2); QVERIFY(originals->urls().contains(QUrl::fromLocalFile(original))); QVERIFY(originals->urls().contains(QUrl::fromLocalFile(quality)));
        sharing->selectAll(); std::unique_ptr<QMimeData> compressed(sharing->dragPayload()); QVERIFY(compressed);
        QCOMPARE(compressed->urls(), QList<QUrl>{QUrl::fromLocalFile(share)});
        QCOMPARE(digest(original), m_digest);
        QVERIFY(dialog->grab().save(previewPath("discord-progress.png"))); dialog->close(); window.setPage(MainWindow::Sharing); QVERIFY(window.grab().save(previewPath("sharing-copies.png"))); window.close();
    }
    void sharingBatchProcessesCompleteClipsContinuesAfterAnInvalidSourceAndCanBeRetried() {
        QTemporaryDir directory; const auto first = directory.filePath("first.mp4"), second = directory.filePath("second.mp4"), late = directory.filePath("late.mp4"); QVERIFY(QFile::copy(m_source, first)); QVERIFY(QFile::copy(m_source, second));
        DiscordBatch batch; QSignalSpy ready(&batch, &DiscordBatch::shareReady), hq(&batch, &DiscordBatch::highQualityReady), finished(&batch, &DiscordBatch::finished);
        ShareOptions options; options.directory = directory.filePath("Partage Discord"); batch.start({first, late, second}, options); QVERIFY(batch.running());
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 2, 10000); QTRY_VERIFY_WITH_TIMEOUT(!batch.running(), 3000); QVERIFY(hq.isEmpty()); QCOMPARE(digest(first), m_digest); QCOMPARE(digest(second), m_digest);
        for (const auto& row : ready) { ClipReader reader(row.first().toString(), false); QCOMPARE(reader.info().size, m_info.size); QVERIFY(qAbs(reader.info().durationMs - m_info.durationMs) <= 50); QCOMPARE(reader.info().audio.size(), 1); }
        QCOMPARE(batch.jobs().size(), 3); QCOMPARE(batch.jobs()[0].state, ShareJob::Ready); QCOMPARE(batch.jobs()[1].state, ShareJob::Failed); QCOMPARE(batch.jobs()[2].state, ShareJob::Ready);
        QVERIFY(!batch.jobs()[1].error.isEmpty()); QVERIFY(batch.jobs()[0].copied); QCOMPARE(batch.percent(), 100); QCOMPARE(finished.last()[0].toInt(), 2); QCOMPARE(finished.last()[1].toInt(), 1);
        // The failed clip waits for a retry; once the file is back, the retry succeeds on its own.
        QVERIFY(QFile::copy(m_source, late)); batch.retry(1); QVERIFY(batch.running());
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 3, 10000); QTRY_VERIFY_WITH_TIMEOUT(!batch.running(), 3000); QCOMPARE(batch.jobs()[1].state, ShareJob::Ready); QCOMPARE(ready.last()[1].toString(), late);
        batch.remove(1); QCOMPARE(batch.jobs().size(), 2);
    }
    void sizeEstimatesAnnounceWhatTheExporterWillDo() {
        ClipExportOptions options; options.maxBytes = 2000000; const auto whole = defaultEdit(m_info); const auto bytes = QFileInfo(m_source).size();
        // Small enough: copied as it is, and announced at its own size.
        auto plan = planSizedExport(m_info, whole, bytes, options); QVERIFY(plan.copyVideo); QVERIFY(plan.feasible); QCOMPARE(plan.estimatedBytes, bytes); QCOMPARE(plan.outputSize, m_info.size);
        // Too big: encoded under the limit, at a size announced below it, possibly at a smaller resolution.
        options.maxBytes = 75000; plan = planSizedExport(m_info, whole, bytes, options); QVERIFY(!plan.copyVideo); QVERIFY(plan.feasible); QVERIFY(plan.estimatedBytes <= options.maxBytes); QVERIFY(plan.estimatedBytes > options.maxBytes * 0.9);
        QVERIFY(plan.outputSize.width() <= m_info.size.width()); QCOMPARE(plan.outputSize.width() % 2, 0);
        const auto path = exported(whole, "estimated.discord.mp4", options); QVERIFY(!path.isEmpty()); QVERIFY(QFileInfo(path).size() <= options.maxBytes);
        ClipReader reader(path, false); QVERIFY(reader.info().size.width() <= plan.outputSize.width()); QVERIFY(reader.info().size.height() <= plan.outputSize.height());
        // An edit can never be a plain copy, and a limit too low for the duration is said before anything starts.
        auto edit = whole; edit.endMs = 1500; options.maxBytes = 2000000; QVERIFY(!planSizedExport(m_info, edit, bytes, options).copyVideo);
        options.maxBytes = 20000; QVERIFY(!planSizedExport(m_info, whole, bytes, options).feasible);
    }
    void cropMouseCoordinatesAndRenderedPreviewMatchTheSource() {
        VideoCanvas canvas; canvas.setAttribute(Qt::WA_DontShowOnScreen); canvas.resize(416, 256); canvas.show();
        QImage image(320, 192, QImage::Format_ARGB32); image.fill(Qt::red);
        for (int y = 48; y < 144; ++y) for (int x = 80; x < 240; ++x) image.setPixelColor(x, y, Qt::green);
        canvas.setFrame(image, image.size()); canvas.setCrop(image.rect()); canvas.setCropTool(true);
        QSignalSpy selected(&canvas, &VideoCanvas::cropSelected);
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, {108, 68});
        QTest::mouseMove(&canvas, {308, 188}); QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, {308, 188});
        QCOMPARE(selected.size(), 1);
        const auto crop = boundedCrop(selected.first().first().toRect(), image.size()); QCOMPARE(crop, QRect(80, 48, 160, 96));
        canvas.setCrop(crop); canvas.setCropTool(false); canvas.setResultPreview(true);
        const auto preview = canvas.grab().toImage();
        QCOMPARE(preview.pixelColor(208, 128), QColor(Qt::green)); QCOMPARE(preview.pixelColor(58, 58), QColor(Qt::green));
        // An accidental click must not replace the current crop.
        canvas.setCropTool(true); QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, {208, 128}); QCOMPARE(selected.size(), 1);
    }
    void editorShowsVideoAndAppliesCropAndAudioControls() {
        ClipEditor editor; editor.setAttribute(Qt::WA_DontShowOnScreen); editor.resize(1050, 740); editor.show(); editor.openClip(m_source);
        auto* player = editor.findChild<ClipPlayer*>(); QVERIFY(player);
        auto* width = editor.findChild<QSpinBox*>("crop-width"); QVERIFY(width);
        QTRY_VERIFY_WITH_TIMEOUT(width->isEnabled() && width->value() == 320, 5000);
        width->setValue(160); editor.findChild<QSpinBox*>("crop-height")->setValue(96); editor.findChild<QSpinBox*>("crop-x")->setValue(80); editor.findChild<QSpinBox*>("crop-y")->setValue(48);
        editor.findChild<QSlider*>("track-volume-0")->setValue(50); editor.findChild<QAbstractButton*>("track-mute-2")->setChecked(true);
        auto* timeline = editor.findChild<ClipTimeline*>(); QVERIFY(timeline); QTRY_VERIFY_WITH_TIMEOUT(timeline->hasAnalysis(), 5000);
        QVERIFY(editor.findChild<QSlider*>("track-volume-0")->isVisible()); QVERIFY(editor.findChild<QAbstractButton*>("track-mute-2")->isVisible());
        QTest::qWait(50); QVERIFY(!player->snapshot().image.isNull()); QCOMPARE(width->value(), 160);
        // Muting keeps the track on the timeline and says so in the export summary.
        QCOMPARE(timeline->volumes().size(), 3); QVERIFY(editor.findChild<QLabel*>("export-audio-summary")->text().contains(QStringLiteral("est muet")));
        QVERIFY(editor.grab().save(previewPath("editor.png")));
        editor.suspend(); QVERIFY(player->snapshot().image.isNull());
    }
    void analysisProducesBoundedRealWaveformsAndThumbnails() {
        const auto analysis = analyzeClip(m_source, m_stop); QVERIFY2(analysis.error.isEmpty(), qPrintable(analysis.error)); QCOMPARE(analysis.peaks.size(), 3);
        for (const auto& peaks : analysis.peaks) { QVERIFY(peaks.size() <= 1400); QVERIFY(!peaks.isEmpty()); const float peak = *std::max_element(peaks.begin(), peaks.end()); QVERIFY(peak > 0.09f && peak < 0.16f); }
        QVERIFY(!analysis.thumbnails.isEmpty()); QVERIFY(analysis.thumbnails.size() <= 10);
        for (const auto& thumbnail : analysis.thumbnails) { QVERIFY(!thumbnail.isNull()); QVERIFY(thumbnail.width() <= 144 && thumbnail.height() <= 82); }
    }
    void timelineHandlesTrimAndWaveformClicksSeekWithinTheSelection() {
        ClipTimeline timeline; timeline.setAttribute(Qt::WA_DontShowOnScreen); timeline.resize(816, 250); timeline.setClip(m_info, defaultEdit(m_info)); timeline.show();
        QSignalSpy trimmed(&timeline, &ClipTimeline::trimChanged), sought(&timeline, &ClipTimeline::seekRequested);
        // 816 px wide: the lane starts at x = 220, after the 208 px header and the 12 px left handle, and ends at x = 803.
        QTest::mousePress(&timeline, Qt::LeftButton, Qt::NoModifier, {220, 50}); QTest::mouseMove(&timeline, {317, 50}); QTest::mouseRelease(&timeline, Qt::LeftButton, Qt::NoModifier, {317, 50});
        QVERIFY(!trimmed.isEmpty()); QVERIFY(qAbs(trimmed.last()[0].toLongLong() - 500) < 15); QCOMPARE(trimmed.last()[1].toLongLong(), m_info.durationMs);
        QTest::mousePress(&timeline, Qt::LeftButton, Qt::NoModifier, {803, 50}); QTest::mouseMove(&timeline, {660, 50}); QTest::mouseRelease(&timeline, Qt::LeftButton, Qt::NoModifier, {660, 50});
        QVERIFY(qAbs(trimmed.last()[1].toLongLong() - 2264) < 20);
        // Grabbing the body of a handle, 8 px away from the cut, must not move the cut by itself.
        const auto end = trimmed.last()[1].toLongLong(); QTest::mouseClick(&timeline, Qt::LeftButton, Qt::NoModifier, {668, 50}); QCOMPARE(trimmed.last()[1].toLongLong(), end);
        QTest::mouseClick(&timeline, Qt::LeftButton, Qt::NoModifier, {463, 106}); QVERIFY(!sought.isEmpty()); QVERIFY(qAbs(sought.last()[0].toLongLong() - 1250) < 20);
        QTest::mouseClick(&timeline, Qt::LeftButton, Qt::NoModifier, {797, 140}); QCOMPARE(sought.last()[0].toLongLong(), trimmed.last()[1].toLongLong() - 1);
    }
    void sourceDialogSavesAutomaticModeAndAdditionalExclusions() {
        auto profile = AppConfig::defaults().profiles.first(); SourceCatalog catalog; catalog.applications = {{"C:/Discord.exe", "Discord"}, {"C:/Music.exe", "Musique"}};
        SourceDialog dialog(catalog, profile, &profile.sources[1], nullptr);
        auto* list = dialog.findChild<QListWidget*>("application-exclusions"); QVERIFY(list); QCOMPARE(list->count(), 2); list->item(1)->setCheckState(Qt::Checked);
        const auto source = dialog.source(); QCOMPARE(source.kind, SourceKind::ApplicationMix); QCOMPARE(source.targetId, QString("@applications")); QCOMPARE(source.excludedApplications, QStringList({"C:/Music.exe"})); QVERIFY(source.excludeSeparateApplications);
    }
    void libraryOpensTheFullEditorAndBackReleasesItsPlayer() {
        const auto configPath = m_directory.filePath("isolated-app-config"); ConfigStore store(configPath); auto config = AppConfig::defaults(); config.libraryPath = m_directory.path(); QString error; QVERIFY2(store.save(config, error), qPrintable(error));
        MainWindow window(configPath); window.setAttribute(Qt::WA_DontShowOnScreen); window.show();
        auto* list = window.findChild<ClipListView*>("clip-library"); QVERIFY(list); QTRY_VERIFY_WITH_TIMEOUT(clipPaths(list).contains(m_source), 3000);
        // Clips are grouped under a date heading, newest first.
        QVERIFY(list->clips()->index(0, 0).data(ClipRole::Header).toBool()); QCOMPARE(list->clips()->index(0, 0).data(ClipRole::Name).toString(), QStringLiteral("Aujourd’hui"));
        const auto index = list->clips()->index(list->rowOf(m_source), 0); list->scrollTo(index); list->viewport()->grab();
        auto* cache = window.findChild<ThumbnailCache*>(); QVERIFY(cache);
        const auto key = index.data(ClipRole::Key).toString();
        QTRY_VERIFY_WITH_TIMEOUT(cache->preview(key) && !cache->preview(key)->image.isNull(), 5000);
        QCOMPARE(cache->preview(key)->info.size, m_info.size); QVERIFY(cache->preview(key)->image.width() <= 128); QCOMPARE(cache->meta(key)->tracks, 3);
        list->selectPaths({m_source}); QVERIFY(!window.findChild<QWidget*>("library-selection-bar")->isHidden()); QVERIFY(window.grab().save(previewPath("library-clips.png"))); list->clearSelection();
        emit list->openRequested(m_source);
        auto* timeline = window.findChild<ClipTimeline*>(); QVERIFY(timeline); QTRY_VERIFY_WITH_TIMEOUT(timeline->hasAnalysis(), 5000);
        // The editor takes the width of the rail; the replay strip stays.
        QVERIFY(window.findChild<QWidget*>("sidebar")->isHidden()); QVERIFY(window.findChild<QWidget*>("strip")->isVisible()); QVERIFY(window.findChild<QSlider*>("track-volume-0")->isVisible()); QVERIFY(window.findChild<QAbstractButton*>("track-mute-2")->isVisible());
        QVERIFY(window.grab().save(previewPath("editor-app.png")));
        auto* player = window.findChild<ClipPlayer*>(); QVERIFY(player); QVERIFY(!player->snapshot().image.isNull());
        // At the minimum window size nothing is pushed out of the window: the inspector keeps its full width.
        window.resize(980, 650); QTest::qWait(100); auto* inspector = window.findChild<QWidget*>("inspector"); QVERIFY(inspector); QVERIFY(inspector->width() <= 248); QVERIFY(inspector->minimumSizeHint().width() <= inspector->width());
        QVERIFY(window.findChild<ClipEditor*>()->width() <= 980); QVERIFY(window.grab().save(previewPath("editor-compact.png")));
        window.findChild<QPushButton*>("editor-back")->click();
        QVERIFY(player->snapshot().image.isNull()); QVERIFY(!window.findChild<QWidget*>("sidebar")->isHidden());
        QTest::qWait(100); QVERIFY(window.grab().save(previewPath("library-compact.png"))); window.close();
    }
    void libraryClassifiesSelectionsReusesCategoriesAndDeletesOnlyVisibleSelectedClips() {
        QTemporaryDir directory; const auto library = directory.filePath("clips"); QVERIFY(QDir().mkpath(library));
        const QStringList names{"Alpha", "Bravo", "Charlie"}; QStringList paths;
        for (const auto& name : names) { const auto path = QDir(library).filePath(name + ".mp4"); QVERIFY(QFile::copy(m_source, path)); paths.append(path); }
        ConfigStore store(directory.filePath("config")); auto config = AppConfig::defaults(); config.libraryPath = library; QString error; QVERIFY(store.save(config, error));
        MainWindow window(directory.filePath("config"), nullptr, [&](const QString& path, QString&) { return QFile::rename(path, directory.filePath(QFileInfo(path).fileName() + ".test-trash")); });
        window.setAttribute(Qt::WA_DontShowOnScreen); window.show(); auto* list = window.findChild<ClipListView*>("clip-library"); QTRY_COMPARE_WITH_TIMEOUT(clipPaths(list).size(), 3, 3000);
        const auto gameOf = [&](const QString& path) { return list->clips()->index(list->rowOf(path), 0).data(ClipRole::Game).toString(); };
        // The safe answer of the trash dialog is the default; the test answers it from inside its event loop.
        bool safeDefault = false; QStringList asked;
        const auto answerTrash = [&](bool confirm) { QTimer::singleShot(0, &window, [&, confirm] {
            auto* dialog = window.findChild<QDialog*>("delete-clips-prompt"); if (!dialog) return;
            asked = dialog->property("names").toStringList(); safeDefault = dialog->findChild<QPushButton*>("trash-keep")->isDefault() && !dialog->findChild<QPushButton*>("trash-confirm")->isDefault();
            if (asked.size() == 2) dialog->grab().save(previewPath("trash-dialog.png"));
            dialog->findChild<QPushButton*>(confirm ? "trash-confirm" : "trash-keep")->click();
        }); };
        list->selectPaths({paths[0], paths[1]}); auto* classify = window.findChild<QPushButton*>("classify-clip"); QVERIFY(classify->isEnabled()); classify->click();
        auto* popup = window.findChild<LibraryPage*>("library-page")->findChild<ClassifyPopup*>(); QVERIFY(popup); QVERIFY(!popup->isHidden()); auto* game = popup->findChild<QLineEdit*>("classify-game"); auto* suggestions = popup->findChild<QListWidget*>(); QVERIFY(game); QVERIFY(suggestions);
        game->setText("FIFA 27"); QCOMPARE(suggestions->item(0)->text(), QStringLiteral("Créer « FIFA 27 »")); QTest::keyClick(game, Qt::Key_Return); QVERIFY(popup->isHidden());
        QTRY_COMPARE_WITH_TIMEOUT(gameOf(paths[0]), QString("FIFA 27"), 3000); QCOMPARE(gameOf(paths[1]), QString("FIFA 27")); QTRY_COMPARE_WITH_TIMEOUT(list->selectedPaths().size(), 2, 3000);
        // An existing category is offered first and applied with Enter alone.
        list->selectPaths({paths[2]}); classify->click(); QVERIFY(!popup->isHidden()); QVERIFY(game->text().isEmpty());
        QCOMPARE(suggestions->item(0)->text(), QString("FIFA 27")); QCOMPARE(suggestions->currentRow(), 0); QTest::qWait(50); QVERIFY(window.grab().save(previewPath("library-classify.png"))); QTest::keyClick(game, Qt::Key_Return);
        QTRY_COMPARE_WITH_TIMEOUT(gameOf(paths[2]), QString("FIFA 27"), 3000);
        // The result is announced by a notification, bottom right, above the selection bar.
        auto* toast = window.findChild<QLabel*>("toast-title"); QVERIFY(toast); QTest::qWait(50); QVERIFY(window.grab().save(previewPath("library-toast.png")));
        LibraryLabels labels(directory.filePath("config/library-tags.json")); QVERIFY(labels.load(error)); for (const auto& path : paths) QCOMPARE(labels.gameFor(path, {}), QString("FIFA 27"));
        // The rail lists the game with its count and filters on a click.
        QPushButton* gameNav = nullptr; for (auto* button : window.findChild<QWidget*>("sidebar")->findChildren<QPushButton*>()) if (button->text() == "FIFA 27") gameNav = button;
        QVERIFY(gameNav); gameNav->click(); QCOMPARE(clipPaths(list).size(), 3); window.findChild<QPushButton*>("nav-clips")->click();
        auto* remove = window.findChild<QPushButton*>("delete-library-clips"); list->selectPaths({paths[0], paths[1]});
        answerTrash(false); remove->click(); QVERIFY(safeDefault); QCOMPARE(asked.size(), 2);
        for (const auto& path : paths) QCOMPARE(digest(path), m_digest);
        list->selectPaths(paths); auto* search = window.findChild<QLineEdit*>("library-search");
        QVERIFY(search); search->setText("Alpha"); QCOMPARE(clipPaths(list), QStringList{paths[0]}); QCOMPARE(window.findChild<QLabel*>("library-selection-count")->text(), QStringLiteral("1 clip sélectionné"));
        answerTrash(true); remove->click();
        QTRY_VERIFY_WITH_TIMEOUT(!QFile::exists(paths[0]), 5000); QCOMPARE(asked, QStringList{"Alpha.mp4"});
        QCOMPARE(digest(paths[1]), m_digest); QCOMPARE(digest(paths[2]), m_digest);
        search->clear(); QTRY_COMPARE_WITH_TIMEOUT(clipPaths(list).size(), 2, 5000); emit list->openRequested(paths[1]);
        auto* player = window.findChild<ClipPlayer*>(); QTRY_VERIFY_WITH_TIMEOUT(player->snapshot().loaded, 5000);
        auto* deleteInEditor = window.findChild<QPushButton*>("delete-editor-clip"); QTRY_VERIFY_WITH_TIMEOUT(deleteInEditor->isEnabled(), 5000);
        answerTrash(true); deleteInEditor->click();
        QTRY_COMPARE_WITH_TIMEOUT(clipPaths(list).size(), 1, 5000); QVERIFY(!player->snapshot().loaded); QVERIFY(!QFile::exists(paths[1])); QCOMPARE(digest(paths[2]), m_digest);
        // The edited copy inherits its category. Its original only leaves through the
        // banner, then the trash dialog. All file moves here use the isolated test operation.
        emit list->openRequested(paths[2]);
        QTRY_VERIFY_WITH_TIMEOUT(player->snapshot().loaded, 5000);
        auto* editor = window.findChild<ClipEditor*>(); auto* width = editor->findChild<QSpinBox*>("crop-width");
        // Decoder readiness precedes the editor's queued initialization. Wait
        // for the controls before changing the crop, just as a user must.
        QTRY_VERIFY_WITH_TIMEOUT(width->isEnabled() && width->value() == 320, 5000); width->setValue(160);
        const auto edited = QDir(library).filePath("Charlie-edited.mp4"); QVERIFY2(editor->exportTo(edited, error), qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(!editor->findChild<QWidget*>("export-banner")->isHidden(), 10000); QVERIFY(QFile::exists(paths[2])); QVERIFY(QFile::exists(edited));
        QTRY_COMPARE_WITH_TIMEOUT(clipPaths(list).size(), 2, 5000); QVERIFY(labels.load(error)); QCOMPARE(labels.gameFor(edited, {}), QString("FIFA 27"));
        answerTrash(true); editor->findChild<QPushButton*>("trash-original")->click(); QCOMPARE(asked, QStringList{"Charlie.mp4"});
        QTRY_VERIFY_WITH_TIMEOUT(!QFile::exists(paths[2]) && QFile::exists(edited) && clipPaths(list) == QStringList{edited}, 10000);
        QVERIFY(!player->snapshot().loaded); QVERIFY(labels.load(error)); QCOMPARE(labels.gameFor(edited, {}), QString("FIFA 27")); QCOMPARE(labels.gameFor(paths[2], "removed"), QString("removed")); window.close();
    }
    void libraryFavouritesRenameAndSortingFollowTheClip() {
        QTemporaryDir directory; const auto library = directory.filePath("clips"); QVERIFY(QDir().mkpath(QDir(library).filePath("Jeu test")));
        const auto alpha = QDir(library).filePath("Jeu test/Alpha.mp4"), bravo = QDir(library).filePath("Bravo.mp4"); QVERIFY(QFile::copy(m_source, alpha)); QVERIFY(QFile::copy(m_source, bravo));
        ConfigStore store(directory.filePath("config")); auto config = AppConfig::defaults(); config.libraryPath = library; QString error; QVERIFY(store.save(config, error));
        MainWindow window(directory.filePath("config")); window.setAttribute(Qt::WA_DontShowOnScreen); window.show();
        auto* list = window.findChild<ClipListView*>("clip-library"); auto* page = window.findChild<LibraryPage*>("library-page"); QTRY_COMPARE_WITH_TIMEOUT(clipPaths(list).size(), 2, 3000);
        const auto data = [&](const QString& path, int role) { return list->clips()->index(list->rowOf(path), 0).data(role); };
        QCOMPARE(data(alpha, ClipRole::Game).toString(), QString("Jeu test")); QVERIFY(!data(alpha, ClipRole::Favorite).toBool());
        // Under the default 20 Mo limit, a small clip is ready to drag as it is.
        QCOMPARE(data(alpha, ClipRole::FitText).toString(), QStringLiteral("Prêt pour Discord")); QVERIFY(data(alpha, ClipRole::FitOk).toBool());
        emit page->favoriteRequested({alpha}, true); QTRY_VERIFY_WITH_TIMEOUT(data(alpha, ClipRole::Favorite).toBool(), 3000); QVERIFY(!data(bravo, ClipRole::Favorite).toBool());
        page->setFilter(LibraryPage::Favorites); QCOMPARE(clipPaths(list), QStringList{alpha}); page->setFilter(LibraryPage::All); QCOMPARE(clipPaths(list).size(), 2);
        // Renaming keeps the folder, the extension, the content, and every mark of the clip.
        emit page->renameRequested(alpha, QStringLiteral("Clutch été 1v3")); const auto renamed = QDir(library).filePath(QStringLiteral("Jeu test/Clutch été 1v3.mp4"));
        QTRY_VERIFY_WITH_TIMEOUT(clipPaths(list).contains(renamed), 5000); QVERIFY(!QFile::exists(alpha)); QCOMPARE(digest(renamed), m_digest); QTRY_VERIFY_WITH_TIMEOUT(data(renamed, ClipRole::Favorite).toBool(), 3000);
        LibraryLabels labels(directory.filePath("config/library-tags.json")); QVERIFY(labels.load(error)); QVERIFY(labels.isFavorite(renamed)); QVERIFY(!labels.isFavorite(alpha));
        // A name already taken, or one that is no file name, changes nothing.
        emit page->renameRequested(bravo, QStringLiteral("a/b")); QTest::qWait(300); QVERIFY(QFile::exists(bravo));
        // Inline rename from the list: F2 opens a field on the name, Enter commits.
        page->rename(bravo); auto* field = list->findChild<QLineEdit*>("clip-rename"); QVERIFY(field); QCOMPARE(field->text(), QString("Bravo")); field->setText("Charlie"); QTest::keyClick(field, Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(clipPaths(list).contains(QDir(library).filePath("Charlie.mp4")), 5000);
        window.close();
    }
    void firstLaunchWelcomesThenSettingsPresetsOnlyFillExistingSettings() {
        QTemporaryDir directory; const auto configPath = directory.filePath("config"); QString error;
        {
            MainWindow window(configPath); window.setAttribute(Qt::WA_DontShowOnScreen); window.show();
            auto* welcome = window.findChild<QWidget*>("onboarding"); QVERIFY(welcome); QVERIFY(welcome->isVisible()); QVERIFY(!window.findChild<QWidget*>("sidebar")->isEnabled());
            // Nothing can start before the four choices are made, and the strip says why.
            QVERIFY(!window.findChild<QPushButton*>("replay-primary")->isEnabled()); QVERIFY(!window.findChild<QPushButton*>("onboarding-start")->isEnabled());
            window.findChild<QPushButton*>("onboarding-next-0")->click(); QVERIFY(window.grab().save(previewPath("onboarding.png")));
            QVERIFY(!QFile::exists(QDir(configPath).filePath("settings.json")));
            window.findChild<QPushButton*>("onboarding-skip")->click();
            QVERIFY(QFile::exists(QDir(configPath).filePath("settings.json"))); QVERIFY(!welcome->isVisible()); QVERIFY(window.findChild<QWidget*>("sidebar")->isEnabled());
            QTRY_VERIFY_WITH_TIMEOUT(window.findChild<QLabel*>("library-summary")->text().startsWith(QStringLiteral("Aucun clip")), 3000); QTest::qWait(50);
            QVERIFY(window.findChild<QPushButton*>("empty-start-replay")->isVisible()); QVERIFY(window.grab().save(previewPath("library-empty.png"))); window.close();
        }
        MainWindow window(configPath); window.setAttribute(Qt::WA_DontShowOnScreen); window.show(); QVERIFY(!window.findChild<QWidget*>("onboarding")->isVisible());
        // The recording has its button beside the replay, and its own shortcut in the settings.
        auto* record = window.findChild<QPushButton*>("record-toggle"); QVERIFY(record); QVERIFY(record->isVisible()); QVERIFY(record->isEnabled()); QCOMPARE(record->text(), QStringLiteral("Enregistrer"));
        QCOMPARE(window.findChild<QKeySequenceEdit*>("record-hotkey")->keySequence(), QKeySequence::fromString("Ctrl+Alt+F9", QKeySequence::PortableText));
        window.setPage(MainWindow::Settings); auto* fps = window.findChild<QComboBox*>("fps"); auto* saveBar = window.findChild<QWidget*>("savebar"); QVERIFY(fps); QVERIFY(saveBar);
        QCOMPARE(fps->currentData().toInt(), 60); QVERIFY(saveBar->isHidden()); QVERIFY(window.findChild<QPushButton*>("quality-1")->isChecked());
        window.findChild<QPushButton*>("quality-0")->click(); QCOMPARE(fps->currentData().toInt(), 30); QVERIFY(window.findChild<QPushButton*>("quality-0")->isChecked()); QVERIFY(!window.findChild<QPushButton*>("quality-1")->isChecked());
        QVERIFY(!saveBar->isHidden()); QCOMPARE(window.findChild<QLabel*>("settings-dirty")->text(), QStringLiteral("2 modifications non enregistrées")); QVERIFY(window.isWindowModified());
        // Ten minutes at this bitrate do not fit under the default ceiling: the estimate says so.
        window.findChild<QSpinBox*>("replay-duration")->setValue(600); QVERIFY(!window.findChild<QWidget*>("memory-warning")->isHidden());
        QVERIFY(window.grab().save(previewPath("settings-capture.png")));
        window.findChild<QPushButton*>("settings-tab-1")->click(); QVERIFY(window.findChild<QWidget*>("audio-lane-0"));
        // Each track has its usual level in the clips; it is a setting like the others, saved or given back with them.
        auto* usual = window.findChild<QSlider*>("track-mix-0"); QVERIFY(usual); QCOMPARE(usual->value(), 100); QVERIFY(!usual->toolTip().isEmpty()); usual->setValue(70);
        QCOMPARE(window.findChild<QLabel*>("settings-dirty")->text(), QStringLiteral("4 modifications non enregistrées"));
        QTest::qWait(50); QVERIFY(window.grab().save(previewPath("settings-audio.png")));
        window.findChild<QPushButton*>("settings-tab-2")->click(); QVERIFY(window.grab().save(previewPath("settings-general.png")));
        // "Annuler" gives the saved settings back; nothing was written meanwhile.
        window.findChild<QPushButton*>("settings-revert")->click(); QCOMPARE(fps->currentData().toInt(), 60); QVERIFY(saveBar->isHidden()); QVERIFY(!window.isWindowModified());
        // The lanes are drawn again with the settings given back; the ones they replace go with the next event.
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); QCOMPARE(window.findChild<QSlider*>("track-mix-0")->value(), 100);
        AppConfig stored; QVERIFY(ConfigStore(configPath).load(stored, error)); QCOMPARE(stored.activeProfile()->video.fps, 60); QVERIFY(stored.defaultMix.isEmpty());
        window.findChild<QSlider*>("track-mix-0")->setValue(70); window.findChild<QSlider*>("track-mix-2")->setValue(130); window.findChild<QPushButton*>("settings-save")->click();
        QVERIFY(ConfigStore(configPath).load(stored, error)); QCOMPARE(stored.defaultMix, (TrackMix{{mixKey("Micro"), 0.7}, {mixKey("Discord"), 1.3}}));
        window.findChild<QSlider*>("track-mix-0")->setValue(100); window.findChild<QSlider*>("track-mix-2")->setValue(100); window.findChild<QPushButton*>("settings-save")->click();
        QVERIFY(ConfigStore(configPath).load(stored, error)); QVERIFY(stored.defaultMix.isEmpty());
        window.findChild<QPushButton*>("quality-2")->click(); window.findChild<QPushButton*>("settings-save")->click(); QVERIFY(saveBar->isHidden());
        QVERIFY(ConfigStore(configPath).load(stored, error)); QCOMPARE(stored.activeProfile()->video.height, 1440); QCOMPARE(stored.activeProfile()->video.bitrateMbps, 50);
        // The last preset holds a constant quality: its level appears, the bitrate turns into a ceiling and the estimate into a maximum.
        auto* mode = window.findChild<QComboBox*>("rate-control"); auto* level = window.findChild<QComboBox*>("quality-level"); QVERIFY(mode); QVERIFY(level); QVERIFY(level->isHidden());
        QCOMPARE(stored.activeProfile()->video.rateControl, QString("bitrate")); QVERIFY(window.findChild<QWidget*>("memory-warning")->isHidden());
        window.findChild<QPushButton*>("quality-3")->click(); QVERIFY(window.findChild<QPushButton*>("quality-3")->isChecked()); QVERIFY(!window.findChild<QPushButton*>("quality-2")->isChecked());
        QCOMPARE(mode->currentData().toString(), QString("quality")); QCOMPARE(level->currentData().toInt(), 14); QVERIFY(!level->isHidden()); QVERIFY(!window.findChild<QWidget*>("memory-warning")->isHidden());
        for (const auto& name : {"rate-control", "quality-level", "effort", "bitrate", "fps"}) QVERIFY(!window.findChild<QWidget*>(name)->toolTip().isEmpty());
        window.findChild<QPushButton*>("settings-tab-0")->click(); window.findChild<QPushButton*>("advanced-toggle")->click(); QTest::qWait(50);
        for (auto* area : window.findChildren<QAbstractScrollArea*>()) if (area->isVisible() && area->findChild<QWidget*>("effort")) area->verticalScrollBar()->setValue(area->verticalScrollBar()->maximum());
        QTest::qWait(50); QVERIFY(window.grab().save(previewPath("settings-quality.png")));
        // The estimate is a range that follows the level, with what the ceiling allows at most.
        auto* memory = window.findChild<QComboBox*>("replay-memory"); memory->setCurrentIndex(memory->findData(2048)); QVERIFY(window.findChild<QWidget*>("memory-warning")->isHidden());
        auto* estimate = window.findChild<QLabel*>("memory-estimate"); const auto nearLossless = estimate->text(); QVERIFY2(nearLossless.contains(QStringLiteral(" à ")) && nearLossless.contains(QStringLiteral("433 au plus")), qPrintable(nearLossless));
        level->setCurrentIndex(level->findData(20)); QVERIFY(estimate->text() != nearLossless); QVERIFY(estimate->text().contains(QStringLiteral("433 au plus")));
        QTest::qWait(50); QVERIFY(window.grab().save(previewPath("settings-quality-range.png"))); level->setCurrentIndex(level->findData(14));
        window.findChild<QComboBox*>("effort")->setCurrentIndex(1); window.findChild<QPushButton*>("settings-save")->click();
        QVERIFY(ConfigStore(configPath).load(stored, error)); QCOMPARE(stored.activeProfile()->video.rateControl, QString("quality")); QCOMPARE(stored.activeProfile()->video.quality, 14);
        QCOMPARE(stored.activeProfile()->video.bitrateMbps, 200); QCOMPARE(stored.activeProfile()->video.effort, QString("thorough"));
        // Back on a target bitrate, the level leaves the page and the preset is recognised again.
        mode->setCurrentIndex(0); window.findChild<QSpinBox*>("bitrate")->setValue(50); QVERIFY(level->isHidden()); QVERIFY(window.findChild<QPushButton*>("quality-2")->isChecked());
        // Ten minutes at 300 Mbit/s weigh on the memory of a PC: past a share of it, the disk is proposed, and can be declined.
        window.findChild<QSpinBox*>("replay-duration")->setValue(600); window.findChild<QSpinBox*>("bitrate")->setValue(300);
        auto expected = AppConfig::defaults().profiles.first(); expected.video.width = 2560; expected.video.height = 1440; expected.video.bitrateMbps = 300; expected.replay.durationSeconds = 600;
        auto* proposal = window.findChild<QWidget*>("disk-proposal"); QVERIFY(proposal); QCOMPARE(proposal->isVisible(), suggestsDiskBuffer(expected, physicalMemoryBytes()));
        if (proposal->isVisible()) {
            for (auto* area : window.findChildren<QScrollArea*>()) if (area->isVisible() && area->widget()->isAncestorOf(proposal)) area->ensureWidgetVisible(proposal);
            QTest::qWait(50); QVERIFY(window.grab().save(previewPath("settings-disk-proposal.png")));
            window.findChild<QPushButton*>("keep-memory-buffer")->click(); QVERIFY(!proposal->isVisible());
        }
        window.findChild<QSpinBox*>("bitrate")->setValue(50);
        // On the disk, the buffer has its own ceiling and folder, beside Clipsapp unless another is chosen; the memory ceiling leaves.
        auto* storage = window.findChild<QComboBox*>("replay-storage"); auto* diskBox = window.findChild<QWidget*>("disk-buffer"); QVERIFY(storage); QVERIFY(diskBox);
        QVERIFY(!diskBox->isVisible()); QVERIFY(memory->isVisible());
        storage->setCurrentIndex(storage->findData("disk")); QVERIFY(diskBox->isVisible()); QVERIFY(!memory->isVisible()); QVERIFY(!proposal->isVisible());
        QVERIFY(window.findChild<QLineEdit*>("replay-disk-folder")->placeholderText().endsWith(QStringLiteral("Buffer du replay")));
        QVERIFY2(window.findChild<QLabel*>("disk-space")->text().contains(QStringLiteral("libres")), qPrintable(window.findChild<QLabel*>("disk-space")->text()));
        QVERIFY2(estimate->text().startsWith(QStringLiteral("Espace estimé sur le disque")), qPrintable(estimate->text()));
        for (auto* area : window.findChildren<QScrollArea*>()) if (area->isVisible() && area->widget()->isAncestorOf(diskBox)) area->ensureWidgetVisible(diskBox);
        QTest::qWait(50); QVERIFY(window.grab().save(previewPath("settings-disk.png")));
        window.findChild<QPushButton*>("settings-revert")->click(); QVERIFY(saveBar->isHidden()); window.close();
    }
    void eachThemeColorsTheWholeWindowWhileItIsOpen() {
        QTemporaryDir directory; const auto library = directory.filePath("clips");
        for (const auto& game : {QStringLiteral("ARC Raiders"), QStringLiteral("FC 27")}) { QVERIFY(QDir().mkpath(library + '/' + game)); for (int index = 0; index < 3; ++index) QVERIFY(QFile::copy(m_source, QStringLiteral("%1/%2/%2-clip-%3.mp4").arg(library, game).arg(index))); }
        { auto config = AppConfig::defaults(); config.libraryPath = library; QString error; QVERIFY(ConfigStore(directory.filePath("config")).save(config, error)); }
        MainWindow window(directory.filePath("config")); window.setAttribute(Qt::WA_DontShowOnScreen); window.show();
        auto* list = window.findChild<ClipListView*>("clip-library"); QTRY_COMPARE_WITH_TIMEOUT(clipPaths(list).size(), 6, 8000); QTest::qWait(400);
        ReplayView view; view.state = ReplayView::Running; view.seconds = 60; view.capacity = 60; view.bytes = 27 << 20; view.canSave = true; view.hotkey = "Alt+F10"; view.recordHotkey = "Ctrl+Alt+F9"; view.liveSummary = QStringLiteral("ARC Raiders détecté · 1440p 60 FPS · h264_nvenc");
        for (const auto& preset : themePresets()) {
            applyColors(preset.colors); window.findChild<ReplayStrip*>()->apply(view);
            window.setPage(MainWindow::Library); QTest::qWait(150); QVERIFY(window.grab().save(previewPath(QStringLiteral("theme-%1-library.png").arg(preset.id))));
            window.setPage(MainWindow::Settings); QTest::qWait(150); QVERIFY(window.grab().save(previewPath(QStringLiteral("theme-%1-settings.png").arg(preset.id))));
        }
        applyColors(ThemeColors{}); window.close();
        // The thumbnails and analyses of its clips end here, rather than during the next test.
        QThreadPool::globalInstance()->waitForDone(15000);
    }
    void aThemeIsChosenTriedSharedAndKept() {
        QTemporaryDir directory; const auto configPath = directory.filePath("config");
        { auto config = AppConfig::defaults(); config.libraryPath = directory.filePath("clips"); QString error; QVERIFY(ConfigStore(configPath).save(config, error)); }
        MainWindow window(configPath); window.setAttribute(Qt::WA_DontShowOnScreen); window.show(); QCOMPARE(tk::accent, ThemeColors{}.accent);
        window.setPage(MainWindow::Settings); window.findChild<QPushButton*>("settings-tab-2")->click();
        const auto violet = themePresets().at(1); QCOMPARE(violet.id, QStringLiteral("violet"));
        // A theme picked shows at once in the whole window, as a change to save.
        window.findChild<QPushButton*>("theme-preset-violet")->click(); QCOMPARE(tk::accent, violet.colors.accent); QVERIFY(tk::windowGradient);
        QVERIFY(window.findChild<QPushButton*>("theme-preset-violet")->isChecked()); QVERIFY(window.findChild<QLabel*>("settings-dirty")->isVisible());
        for (auto* area : window.findChildren<QScrollArea*>()) if (area->isVisible() && area->widget()->isAncestorOf(window.findChild<QWidget*>("theme-editor"))) area->ensureWidgetVisible(window.findChild<QWidget*>("theme-editor"), 0, 0);
        QTest::qWait(100); QVERIFY(window.grab().save(previewPath("settings-appearance-violet.png")));
        // In the picker, each color tried is the theme's at once; cancelled, the one before comes back.
        bool tried = false;
        QTimer::singleShot(300, &window, [&] {
            auto* picker = window.findChild<QDialog*>("color-picker"); if (!picker) return;
            auto* code = picker->findChild<QLineEdit*>("color-code"); code->selectAll(); QTest::keyClicks(code, QStringLiteral("#FF5A36"));
            tried = tk::accent == QColor(0xFF, 0x5A, 0x36); QTest::qWait(50); picker->grab().save(previewPath("theme-color-picker.png")); window.grab().save(previewPath("theme-color-trying.png")); picker->reject();
        });
        window.findChild<QPushButton*>("theme-accent")->click(); QVERIFY(tried); QCOMPARE(tk::accent, violet.colors.accent);
        // Copied, it is one line; given back, the default theme returns; pasted, even inside a message, it comes back whole.
        window.findChild<QPushButton*>("theme-copy")->click(); const auto code = QApplication::clipboard()->text(); QVERIFY(code.startsWith(QStringLiteral("clipsapp-theme-")));
        window.findChild<QPushButton*>("theme-reset")->click(); QCOMPARE(tk::accent, ThemeColors{}.accent);
        QApplication::clipboard()->setText(QStringLiteral("Essaie mon thème : %1 !").arg(code)); window.findChild<QPushButton*>("theme-paste")->click(); QCOMPARE(tk::accent, violet.colors.accent);
        QApplication::clipboard()->setText(QStringLiteral("rien")); window.findChild<QPushButton*>("theme-paste")->click(); QCOMPARE(tk::accent, violet.colors.accent);
        // Saved, it is in the settings, and its icon is written for the Start menu.
        window.findChild<QPushButton*>("settings-save")->click();
        AppConfig stored; QString error; QVERIFY(ConfigStore(configPath).load(stored, error)); QCOMPARE(themeFromSettings(stored.theme), violet.colors);
        const auto icons = QDir(configPath).entryList({QStringLiteral("Clipsapp-*.ico")}, QDir::Files); QCOMPARE(icons.size(), 1);
        QVERIFY(QIcon(QDir(configPath).filePath(icons.first())).availableSizes().contains(QSize(256, 256)));
        // Tried and given back, the saved theme returns.
        window.findChild<QPushButton*>("theme-preset-amber")->click(); QCOMPARE(tk::accent, themePresets().at(3).colors.accent);
        window.findChild<QPushButton*>("settings-revert")->click(); QCOMPARE(tk::accent, violet.colors.accent);
        // Back to the default theme, the program's own icon serves again.
        window.findChild<QPushButton*>("theme-reset")->click(); window.findChild<QPushButton*>("settings-save")->click();
        QVERIFY(QDir(configPath).entryList({QStringLiteral("Clipsapp-*.ico")}, QDir::Files).isEmpty()); QCOMPARE(tk::accent, ThemeColors{}.accent);
        window.close(); QThreadPool::globalInstance()->waitForDone(15000);
    }
    void duringACaptureOnlyWhatItUsesWaits() {
        QTemporaryDir directory; const auto configPath = directory.filePath("config");
        { auto config = AppConfig::defaults(); config.libraryPath = directory.filePath("clips"); QString error; QVERIFY(ConfigStore(configPath).save(config, error)); }
        MainWindow window(configPath); window.setAttribute(Qt::WA_DontShowOnScreen); window.show(); window.setPage(MainWindow::Settings);
        auto* settings = window.findChild<SettingsPage*>(); QVERIFY(settings); settings->setLocked(true);
        // The audio rows are drawn again; those they replace go with the next event.
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        const auto enabled = [&window](const char* name) { auto* widget = window.findChild<QWidget*>(name); return widget && widget->isEnabled(); };
        // What the capture uses waits: what is captured, its quality, the replay's buffer, the sources, the profile.
        for (const char* name : {"video-target", "quality-0", "replay-duration", "replay-storage", "replay-memory", "add-source", "source-edit-0"}) QVERIFY2(!enabled(name), name);
        QVERIFY(!window.findChild<QComboBox*>("profile-choice") || !window.findChild<QComboBox*>("profile-choice")->isEnabled());
        // The rest applies at once: shortcuts, folder, usual volumes, themes and the whole General tab.
        for (const char* name : {"record-hotkey-change", "screenshot-hotkey-change", "libraryPath", "track-mix-0", "theme-preset-violet", "clip-saved-sound", "settings-tab-0", "settings-tab-1", "settings-tab-2"}) QVERIFY2(enabled(name), name);
        QVERIFY(window.findChild<QWidget*>("settings-locked")->isVisible());
        window.findChild<QPushButton*>("settings-tab-0")->click(); QTest::qWait(100); QVERIFY(window.grab().save(previewPath("settings-locked-capture.png")));
        window.findChild<QPushButton*>("settings-tab-1")->click(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); QTest::qWait(100); QVERIFY(window.grab().save(previewPath("settings-locked-audio.png")));
        // Changed and saved during the capture, without stopping it.
        window.findChild<QPushButton*>("settings-tab-2")->click(); window.findChild<QPushButton*>("theme-preset-violet")->click();
        QVERIFY(window.findChild<QWidget*>("savebar")->isVisible()); window.findChild<QPushButton*>("settings-save")->click();
        AppConfig stored; QString error; QVERIFY(ConfigStore(configPath).load(stored, error)); QCOMPARE(themeFromSettings(stored.theme), themePresets().at(1).colors);
        settings->setLocked(false); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); for (const char* name : {"video-target", "replay-duration", "add-source", "source-edit-0"}) QVERIFY2(enabled(name), name);
        window.findChild<QPushButton*>("theme-reset")->click(); window.findChild<QPushButton*>("settings-save")->click(); window.close();
    }
    void aThemeTravelsAsOneLineAndStaysDark() {
        auto colors = themePresets().at(1).colors; ThemeColors read;
        QVERIFY(readThemeCode(themeCode(colors), read)); QCOMPARE(read, colors);
        QVERIFY(readThemeCode(QStringLiteral("> CLIPSAPP-THEME-0f0b17-a98bff-eeeaf7-0-0-90"), read)); QVERIFY(!read.windowGradient); QVERIFY(!read.accentGradient); QCOMPARE(read.angle, 90);
        QVERIFY(!readThemeCode(QStringLiteral("clipsapp-theme-zzzzzz"), read));
        // A light background is darkened, a dark text lightened: the themes stay dark for now.
        colors.background = QColor(Qt::white); colors.text = QColor(Qt::black); const auto dark = darkTheme(colors);
        QVERIFY(dark.background.lightnessF() <= 0.221); QVERIFY(dark.text.lightnessF() >= 0.719);
        QCOMPARE(themeFromSettings(themeSettings(ThemeColors{})), ThemeColors{}); QVERIFY(themeSettings(ThemeColors{}).background.isEmpty());
    }
    void theStripShowsTheReplayAndTheRecordingEachInItsOwnBox() {
        ReplayStrip strip; strip.resize(1400, 60); strip.setAttribute(Qt::WA_DontShowOnScreen); strip.show();
        auto* replayBox = strip.findChild<QWidget*>("replay-status"); auto* recordBox = strip.findChild<QWidget*>("record-status");
        auto* record = strip.findChild<QPushButton*>("record-toggle"); auto* primary = strip.findChild<QPushButton*>("replay-primary"); auto* secondary = strip.findChild<QPushButton*>("replay-secondary");
        ReplayView view; view.hotkey = "Alt+F10"; view.recordHotkey = "Ctrl+Alt+F9"; view.capacity = 60; view.liveSummary = QStringLiteral("1440p 60 FPS · h264_nvenc"); view.profileSummary = QStringLiteral("Profil Principal · Écran 1");
        const auto shot = [&](const QString& name) { QApplication::processEvents(); QVERIFY(strip.grab().save(previewPath(name))); };
        // Nothing runs: the replay's box says so; one button records, the other starts the replay.
        strip.apply(view); QVERIFY(replayBox->isVisible()); QVERIFY(!recordBox->isVisible()); QCOMPARE(record->text(), QStringLiteral("Enregistrer")); QCOMPARE(primary->text(), QStringLiteral("Démarrer le replay")); QVERIFY(!secondary->isVisible()); shot("strip-idle.png");
        // The replay alone, in green: save and stop it.
        view.state = ReplayView::Running; view.seconds = 60; view.bytes = 27 << 20; view.canSave = true; strip.apply(view);
        QVERIFY(replayBox->isVisible()); QVERIFY(!recordBox->isVisible()); QCOMPARE(primary->text(), QStringLiteral("Sauvegarder le clip")); QVERIFY(secondary->isVisible()); QCOMPARE(secondary->text(), QStringLiteral("Arrêter replay")); shot("strip-replay.png");
        // The recording alone, in red: no replay bar, the replay can be started, nothing stops a replay that does not run.
        view.replay = false; view.recording = true; view.recordingSeconds = 79; view.recordingBytes = 31 << 20; view.seconds = 0; view.canSave = false; strip.apply(view);
        QVERIFY(!replayBox->isVisible()); QVERIFY(recordBox->isVisible()); QCOMPARE(record->text(), QStringLiteral("Arrêter l'enregistrement")); QCOMPARE(primary->text(), QStringLiteral("Démarrer le replay")); QVERIFY(!secondary->isVisible()); shot("strip-recording.png");
        // Both: two boxes side by side, each with its own buttons.
        view.replay = true; view.seconds = 60; view.canSave = true; strip.apply(view);
        QVERIFY(replayBox->isVisible()); QVERIFY(recordBox->isVisible()); QCOMPARE(primary->text(), QStringLiteral("Sauvegarder le clip")); QVERIFY(secondary->isVisible()); QCOMPARE(secondary->text(), QStringLiteral("Arrêter replay"));
        // At any width, refreshed again and again as the replay does twice a second, it keeps one look,
        // and that look fits: it never hesitates between the full one and the compact one.
        for (const auto& shown : {std::pair{true, false}, std::pair{true, true}}) {
            auto steady = view; steady.replay = shown.first; steady.recording = shown.second; steady.seconds = 60; steady.bytes = 220 << 20; steady.liveSummary = QStringLiteral("EA SPORTS FC™ 27 détecté · 1440p 60 FPS · h264_nvenc");
            for (int width = 980; width <= 1400; width += 20) {
                strip.apply(steady); QApplication::processEvents(); strip.resize(width, 60); QApplication::processEvents();
                const auto first = secondary->text(); strip.apply(steady); QApplication::processEvents(); strip.apply(steady); QApplication::processEvents();
                QVERIFY2(secondary->text() == first, qPrintable(QStringLiteral("%1 px : %2 puis %3").arg(width).arg(first, secondary->text())));
                for (auto* layout : strip.findChildren<QLayout*>()) layout->invalidate(); strip.layout()->invalidate();
                QVERIFY2(strip.layout()->minimumSize().width() <= width, qPrintable(QStringLiteral("%1 px, replay %2, enregistrement %3, %4 : %5 nécessaires").arg(width).arg(shown.first).arg(shown.second).arg(secondary->text()).arg(strip.layout()->minimumSize().width())));
            }
        }
        // Both fit the window at its usual width and at its narrowest, beside the brand.
        for (const int width : {1220, 980}) {
            strip.resize(width, 60); QApplication::processEvents(); strip.apply(view); QApplication::processEvents(); strip.resize(width, 60); QApplication::processEvents();
            QVERIFY2(strip.layout()->minimumSize().width() <= width, qPrintable(QStringLiteral("%1 > %2").arg(strip.layout()->minimumSize().width()).arg(width)));
            QCOMPARE(strip.width(), width); shot(QStringLiteral("strip-both-%1.png").arg(width));
        }
    }
    void closingTheWindowLeavesTheApplicationBehindItsIconUntilQuit() {
        QTemporaryDir directory; const auto configPath = directory.filePath("config"); QString error;
        // The first time, a real Windows notification says where the window went: here it has been said.
        { auto config = AppConfig::defaults(); config.trayNoticeShown = true; QVERIFY2(ConfigStore(configPath).save(config, error), qPrintable(error)); }
        MainWindow window(configPath); window.setAttribute(Qt::WA_DontShowOnScreen); window.show();
        auto* tray = window.findChild<QSystemTrayIcon*>(); QVERIFY(tray); auto* menu = window.findChild<QMenu*>("tray-menu"); QVERIFY(menu);
        if (!QSystemTrayIcon::isSystemTrayAvailable()) QSKIP("No notification area on this system.");
        // A window that was never on screen has no icon to hide behind. The test keeps it off screen
        // and only lifts the mark, so that the icon comes with the next refresh of the settings.
        QVERIFY(!tray->isVisible()); window.setAttribute(Qt::WA_DontShowOnScreen, false);
        window.setPage(MainWindow::Settings); window.findChild<QPushButton*>("settings-tab-2")->click();
        auto* sound = window.findChild<QCheckBox*>("clip-saved-sound"); sound->setChecked(!sound->isChecked()); window.findChild<QPushButton*>("settings-save")->click(); QVERIFY(tray->isVisible());
        QStringList entries; for (const auto* action : menu->actions()) if (!action->isSeparator()) entries.append(action->text());
        QCOMPARE(entries, QStringList({QStringLiteral("Ouvrir Clipsapp"), QStringLiteral("Sauvegarder le clip"), QStringLiteral("Prendre un screenshot"), QStringLiteral("Démarrer un enregistrement"), QStringLiteral("Démarrer le replay"), QStringLiteral("Quitter Clipsapp")}));
        QVERIFY(!menu->actions().at(2)->isEnabled());
        // Closing hides the window and ends nothing; unsaved settings are not asked about, they are still there.
        sound->setChecked(!sound->isChecked()); QVERIFY(!window.close()); QVERIFY(window.isHidden()); QVERIFY(tray->isVisible());
        window.setAttribute(Qt::WA_DontShowOnScreen); emit tray->activated(QSystemTrayIcon::Context); QVERIFY(window.isHidden());
        emit tray->activated(QSystemTrayIcon::Trigger); QVERIFY(window.isVisible()); QVERIFY(!window.findChild<QWidget*>("savebar")->isHidden());
        window.findChild<QPushButton*>("settings-revert")->click(); window.setAttribute(Qt::WA_DontShowOnScreen, false);
        QVERIFY(!window.close()); QVERIFY(window.isHidden());
        // "Quitter", in the menu of the icon, is what ends the application: the icon leaves with it.
        menu->actions().last()->trigger(); QVERIFY(!tray->isVisible());
        // With the setting off, the window closes as any other.
        window.setAttribute(Qt::WA_DontShowOnScreen); window.show(); window.setAttribute(Qt::WA_DontShowOnScreen, false);
        auto* keep = window.findChild<QCheckBox*>("close-to-tray"); QVERIFY(keep->isChecked()); keep->setChecked(false); window.findChild<QPushButton*>("settings-save")->click();
        QVERIFY(window.close()); QVERIFY(!tray->isVisible());
        AppConfig stored; QVERIFY(ConfigStore(configPath).load(stored, error)); QVERIFY(!stored.closeToTray);
    }
    void presentationImagesAreTheCroppedResultAtTheSizeOfTheScreen() {
        // The reader hands out the region asked for, fitted to the box: no pixel is rescaled when it is drawn.
        ClipReader whole(m_source, false), region(m_source, false), fitted(m_source, false);
        QVERIFY(!whole.setFrameOutput({})); QCOMPARE(whole.frameSize(), m_info.size);
        FrameOutput output; output.fit = true; output.box = {160, 96}; output.crop = {80, 48, 160, 96}; QVERIFY(region.setFrameOutput(output)); QVERIFY(!region.setFrameOutput(output)); QCOMPARE(region.frameSize(), QSize(160, 96));
        output.box = {1280, 720}; output.crop = {}; QVERIFY(fitted.setFrameOutput(output)); QCOMPARE(fitted.frameSize(), QSize(1200, 720));
        for (auto* reader : {&whole, &region, &fitted}) { reader->seek(1000); reader->readUntil(1001, m_stop, true); }
        const auto full = whole.frameAt(1000), part = region.frameAt(1000), large = fitted.frameAt(1000);
        QCOMPARE(full.size(), m_info.size); QCOMPARE(part.size(), QSize(160, 96)); QCOMPARE(large.size(), QSize(1200, 720)); QCOMPARE(part.format(), QImage::Format_RGB32);
        // Same frame, same pixels: the cropped image is the region of the whole one. Chroma is shared by 2 x 2 pixels, hence the tolerance.
        int worst = 0; const auto cut = full.copy(80, 48, 160, 96);
        for (int y = 4; y < 92; y += 3) for (int x = 4; x < 156; x += 3) { const QColor a = cut.pixelColor(x, y), b = part.pixelColor(x, y); worst = std::max({worst, qAbs(a.red() - b.red()), qAbs(a.green() - b.green()), qAbs(a.blue() - b.blue())}); }
        QVERIFY2(worst <= 12, qPrintable(QString::number(worst)));
        // The player in presentation plays the edit's crop at the box size, and keeps the light 720p preview otherwise.
        ClipPlayer player(nullptr, false); player.setPresentation({640, 384}); player.load(m_source); QTRY_VERIFY_WITH_TIMEOUT(player.snapshot().loaded && !player.snapshot().loading, 5000);
        QCOMPARE(player.snapshot().image.size(), QSize(640, 384));
        auto edit = defaultEdit(m_info); edit.crop = {80, 48, 96, 96}; player.setEdit(edit); QTRY_COMPARE_WITH_TIMEOUT(player.snapshot().image.size(), QSize(384, 384), 5000);
        player.setPlaying(true); QTRY_VERIFY_WITH_TIMEOUT(player.snapshot().positionMs > 300, 3000); QCOMPARE(player.snapshot().image.size(), QSize(384, 384)); player.close();
        ClipPlayer preview(nullptr, false); preview.load(m_source); QTRY_VERIFY_WITH_TIMEOUT(preview.snapshot().loaded && !preview.snapshot().loading, 5000); QCOMPARE(preview.snapshot().image.size(), m_info.size); preview.close();
        QCOMPARE(digest(m_source), m_digest);
    }
    void fullScreenViewerPlaysAtItsOwnSizeWalksThePlaylistAndHandsThePositionBack() {
        QTemporaryDir directory; const auto second = directory.filePath(QStringLiteral("Deuxième clip.mp4")); QVERIFY(QFile::copy(m_source, second));
        ClipViewer viewer(nullptr); viewer.setAttribute(Qt::WA_DontShowOnScreen); viewer.resize(960, 576);
        QSignalSpy closed(&viewer, &ClipViewer::closed), viewed(&viewer, &ClipViewer::viewed);
        auto* player = viewer.findChild<ClipPlayer*>(); QVERIFY(player);
        viewer.open({m_source, second}, 0); QVERIFY(viewer.isVisible());
        // Opened, it plays by itself, at the size of the window: three times the clip here.
        QTRY_VERIFY_WITH_TIMEOUT(player->snapshot().playing && player->snapshot().positionMs > 200, 5000); QCOMPARE(viewed.size(), 1); QCOMPARE(viewed.first().first().toString(), m_source);
        QTRY_COMPARE_WITH_TIMEOUT(viewer.image().size(), QSize(960, 576), 3000);
        QCOMPARE(static_cast<ElideLabel*>(viewer.findChild<QLabel*>("viewer-title"))->fullText(), QStringLiteral("Original avec été")); QCOMPARE(viewer.findChild<QLabel*>("viewer-count")->text(), QStringLiteral("1 sur 2"));
        QVERIFY(viewer.grab().save(previewPath("viewer.png")));
        QTest::keyClick(&viewer, Qt::Key_Space); QTRY_VERIFY_WITH_TIMEOUT(!player->snapshot().playing && !player->snapshot().loading, 3000);
        const auto paused = player->snapshot().positionMs; const auto frame = qRound64(1000.0 / m_info.fps);
        // One image forward, one back: the two keys around the left hand's forward key, then their buttons.
        const auto keys = stepKeys(); auto* stepNext = viewer.findChild<QPushButton*>("viewer-frame-next"); auto* stepPrevious = viewer.findChild<QPushButton*>("viewer-frame-previous");
        QTest::keyClick(&viewer, keys.forward); QTRY_COMPARE_WITH_TIMEOUT(player->snapshot().positionMs, paused + frame, 3000); QTest::keyClick(&viewer, keys.back); QTRY_COMPARE_WITH_TIMEOUT(player->snapshot().positionMs, paused, 3000);
        stepNext->click(); QTRY_COMPARE_WITH_TIMEOUT(player->snapshot().positionMs, paused + frame, 3000); stepPrevious->click(); QTRY_COMPARE_WITH_TIMEOUT(player->snapshot().positionMs, paused, 3000);
        QCOMPARE(stepNext->toolTip(), QStringLiteral("Image suivante (%1)").arg(keys.forwardLabel)); QCOMPARE(stepPrevious->toolTip(), QStringLiteral("Image précédente (%1)").arg(keys.backLabel));
        // Volume: the slider, the wheel wherever the pointer is, and the mute button. The clip is not touched.
        QSignalSpy volume(&viewer, &ClipViewer::volumeChanged); auto* level = viewer.findChild<QSlider*>("viewer-volume"); auto* mute = viewer.findChild<QPushButton*>("viewer-mute"); auto* bar = viewer.findChild<QSlider*>("viewer-seek");
        // Sent through the window, as the mouse does: only such a wheel event travels from a control up to the viewer.
        const auto wheel = [&viewer](QWidget* target, int delta) { const QPoint spot = target == &viewer ? QPoint(viewer.width() / 2, viewer.height() / 2) : target->mapTo(&viewer, QPoint(4, 4)); QTest::wheelEvent(viewer.windowHandle(), QPointF(spot), QPoint(0, delta)); QCoreApplication::processEvents(); };
        QCOMPARE(viewer.volume(), 100); QCOMPARE(level->value(), 100); QCOMPARE(player->volume(), 1.0);
        level->setValue(60); QCOMPARE(viewer.volume(), 60); QVERIFY(qAbs(player->volume() - 0.36) < 1e-6); QCOMPARE(volume.size(), 1); QCOMPARE(volume.first().first().toInt(), 60);
        wheel(&viewer, 120); QCOMPARE(viewer.volume(), 65); wheel(&viewer, -240); QCOMPARE(viewer.volume(), 55); QCOMPARE(level->value(), 55);
        const int placed = bar->value(); wheel(bar, 120); QCOMPARE(viewer.volume(), 60); QCOMPARE(bar->value(), placed); wheel(level, 120); QCOMPARE(viewer.volume(), 65);
        // A touchpad sends parts of a notch: they add up.
        wheel(&viewer, 60); QCOMPARE(viewer.volume(), 65); wheel(&viewer, 60); QCOMPARE(viewer.volume(), 70);
        mute->click(); QVERIFY(viewer.muted()); QCOMPARE(player->volume(), 0.0); QCOMPARE(viewer.volume(), 70); QCOMPARE(level->value(), 70);
        wheel(&viewer, -120); QVERIFY(viewer.muted()); QCOMPARE(viewer.volume(), 65); QCOMPARE(player->volume(), 0.0);
        QTest::keyClick(&viewer, Qt::Key_M); QVERIFY(!viewer.muted()); QVERIFY(qAbs(player->volume() - 0.65 * 0.65) < 1e-6);
        mute->click(); wheel(&viewer, 120); QVERIFY(!viewer.muted()); QCOMPARE(viewer.volume(), 70);
        for (int notch = 0; notch < 30; ++notch) wheel(&viewer, -120);
        QCOMPARE(viewer.volume(), 0); QCOMPARE(player->volume(), 0.0); mute->click(); QCOMPARE(viewer.volume(), 50); viewer.setVolume(100); QCOMPARE(player->volume(), 1.0);
        // The level moves in a ramp across a block of samples, never in one jump.
        std::vector<float> block(960, 1.f); QCOMPARE(applyGain(block.data(), 480, 1.f, 1.f), 1.f); QCOMPARE(block[0], 1.f); QCOMPARE(applyGain(block.data(), 480, 1.f, 0.25f), 0.25f);
        QVERIFY(block[0] < 1.f && block[0] > 0.99f); QVERIFY(qAbs(block[959] - 0.25f) < 1e-4f); for (int frame = 1; frame < 480; ++frame) { QVERIFY(block[2 * frame] < block[2 * frame - 2]); QCOMPARE(block[2 * frame + 1], block[2 * frame]); }
        QVERIFY(viewer.grab().save(previewPath("viewer-paused.png")));
        QTest::keyClick(&viewer, Qt::Key_Home); QTRY_COMPARE_WITH_TIMEOUT(player->snapshot().positionMs, qint64(0), 3000);
        // Another size of window, as on another screen: the same clip, redrawn for it.
        viewer.resize(640, 384); QTRY_COMPARE_WITH_TIMEOUT(viewer.image().size(), QSize(640, 384), 3000); QCOMPARE(player->snapshot().info.path, m_source);
        auto* seek = viewer.findChild<QSlider*>("viewer-seek"); QCOMPARE(seek->maximum(), int(m_info.durationMs)); emit seek->sliderMoved(1500); QTRY_COMPARE_WITH_TIMEOUT(player->snapshot().positionMs, qint64(1500), 3000);
        // The next clip of the list, then back; the ends of the list stay where they are.
        QTest::keyClick(&viewer, Qt::Key_PageDown); QCOMPARE(viewer.currentPath(), second); QCOMPARE(viewer.findChild<QLabel*>("viewer-count")->text(), QStringLiteral("2 sur 2"));
        QTRY_VERIFY_WITH_TIMEOUT(player->snapshot().playing && player->snapshot().info.path == second, 5000); QCOMPARE(viewed.size(), 2);
        QTest::keyClick(&viewer, Qt::Key_PageDown); QCOMPARE(viewer.currentPath(), second); QVERIFY(!viewer.findChild<QPushButton*>("viewer-next")->isEnabled());
        viewer.findChild<QPushButton*>("viewer-previous")->click(); QCOMPARE(viewer.currentPath(), m_source); QTRY_VERIFY_WITH_TIMEOUT(player->snapshot().playing, 5000);
        QTest::keyClick(&viewer, Qt::Key_Escape); QCOMPARE(closed.size(), 1); QCOMPARE(closed.first().first().toString(), m_source); QVERIFY(viewer.isHidden()); QVERIFY(!player->snapshot().loaded); QVERIFY(viewer.image().isNull());
        // From the editor: the edit is what is shown, from where the editor was.
        auto edit = defaultEdit(m_info); edit.crop = {80, 48, 96, 96}; edit.startMs = 500; edit.endMs = 2500;
        viewer.open({m_source}, 0, edit, 1200); QTRY_VERIFY_WITH_TIMEOUT(player->snapshot().playing && player->snapshot().positionMs >= 1200, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(viewer.image().size(), QSize(384, 384), 3000); QCOMPARE(seek->maximum(), 2000); QVERIFY(viewer.findChild<QLabel*>("viewer-count")->isHidden()); QVERIFY(viewer.findChild<QPushButton*>("viewer-next")->isHidden());
        QTest::keyClick(&viewer, Qt::Key_Space); QTRY_VERIFY_WITH_TIMEOUT(!player->snapshot().playing && !player->snapshot().loading, 3000); const auto stopped = player->snapshot().positionMs;
        viewer.findChild<QPushButton*>("viewer-close")->click(); QCOMPARE(closed.size(), 2); QCOMPARE(closed.last()[1].toLongLong(), stopped); QVERIFY(stopped >= 1200 && stopped <= 2500);
        QCOMPARE(digest(m_source), m_digest);
    }
    void fullScreenOpensFromTheLibraryAndShowsTheEditorsResult() {
        QTemporaryDir directory; const auto first = directory.filePath("Premier.mp4"), second = directory.filePath("Second.mp4"); QVERIFY(QFile::copy(m_source, first)); QVERIFY(QFile::copy(m_source, second));
        ConfigStore store(directory.filePath("config")); auto config = AppConfig::defaults(); config.libraryPath = directory.path(); QString error; QVERIFY(store.save(config, error));
        MainWindow window(directory.filePath("config")); window.setAttribute(Qt::WA_DontShowOnScreen); window.show();
        auto* list = window.findChild<ClipListView*>("clip-library"); QTRY_COMPARE_WITH_TIMEOUT(clipPaths(list).size(), 2, 3000); const auto listed = clipPaths(list);
        // The round button on a thumbnail plays that clip; the others listed follow it.
        const QPointF spot = list->visualRect(list->clips()->index(1, 0)).topLeft() + QPointF(70, 30); QHoverEvent hover(QEvent::HoverMove, spot, list->viewport()->mapToGlobal(spot), QPointF(-1, -1)); QApplication::sendEvent(list->viewport(), &hover);
        QTest::mouseMove(list->viewport(), spot.toPoint()); QVERIFY(window.grab().save(previewPath("library-hover.png")));
        emit list->actionRequested(ClipRowDelegate::Play, listed[1], {});
        auto* viewer = window.findChild<ClipViewer*>(); QVERIFY(viewer); QVERIFY(viewer->isVisible()); QCOMPARE(viewer->currentPath(), listed[1]); QCOMPARE(viewer->findChild<QLabel*>("viewer-count")->text(), QStringLiteral("2 sur 2"));
        auto* showing = viewer->findChild<ClipPlayer*>(); QTRY_VERIFY_WITH_TIMEOUT(showing->snapshot().playing && !viewer->image().isNull(), 5000);
        // The window finds its size: the image follows, without loading the clip again.
        viewer->resize(600, 360); const auto ratio = viewer->devicePixelRatioF(); const QSize screen(qRound(600 * ratio), qRound(360 * ratio));
        QTRY_COMPARE_WITH_TIMEOUT(viewer->image().size(), screen, 3000); QVERIFY(showing->snapshot().playing);
        QCOMPARE(viewer->volume(), 100); viewer->findChild<QSlider*>("viewer-volume")->setValue(40);
        QTest::keyClick(viewer, Qt::Key_PageUp); QCOMPARE(viewer->currentPath(), listed[0]); QTest::keyClick(viewer, Qt::Key_Escape); QVERIFY(viewer->isHidden());
        // The level chosen is kept for the next viewing, and for the next launch.
        { AppConfig kept; QString problem; QVERIFY(ConfigStore(directory.filePath("config")).load(kept, problem)); QCOMPARE(kept.playbackVolume, 40); }
        // Space on the selected clip does the same.
        list->setCurrentIndex(list->clips()->index(list->rowOf(first), 0)); int spaces = 0;
        for (auto* shortcut : list->findChildren<QShortcut*>()) if (shortcut->key() == QKeySequence(Qt::Key_Space)) { ++spaces; emit shortcut->activated(); }
        QCOMPARE(spaces, 1); QVERIFY(viewer->isVisible()); QCOMPARE(viewer->currentPath(), first); QCOMPARE(viewer->volume(), 40); QVERIFY(qAbs(showing->volume() - 0.16) < 1e-6); QTest::keyClick(viewer, Qt::Key_Escape); QVERIFY(viewer->isHidden());
        // From the editor, the result is what plays: cropped, from where the editor is. Leaving puts the editor where the viewing stopped.
        emit list->openRequested(first); auto* editor = window.findChild<ClipEditor*>(); auto* width = editor->findChild<QSpinBox*>("crop-width"); QTRY_VERIFY_WITH_TIMEOUT(width->isEnabled(), 5000);
        auto* button = editor->findChild<QPushButton*>("editor-full-screen"); QVERIFY(button); QVERIFY(button->isEnabled()); width->setValue(160);
        auto* editing = editor->findChild<ClipPlayer*>(); editing->seek(1200); QTRY_VERIFY_WITH_TIMEOUT(!editing->snapshot().loading && editing->snapshot().positionMs == 1200, 3000);
        button->click(); QVERIFY(viewer->isVisible()); QCOMPARE(viewer->currentPath(), first); QVERIFY(viewer->findChild<QLabel*>("viewer-count")->isHidden());
        QTRY_VERIFY_WITH_TIMEOUT(showing->snapshot().playing && showing->snapshot().positionMs >= 1200, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(viewer->image().size(), QSize(160, 192).scaled(screen, Qt::KeepAspectRatio), 3000);
        QTest::keyClick(viewer, Qt::Key_Space); QTRY_VERIFY_WITH_TIMEOUT(!showing->snapshot().playing && !showing->snapshot().loading, 3000); const auto stopped = showing->snapshot().positionMs; QVERIFY(stopped >= 1200);
        QTest::keyClick(viewer, Qt::Key_Escape); QVERIFY(viewer->isHidden()); QTRY_COMPARE_WITH_TIMEOUT(editing->snapshot().positionMs, stopped, 3000); QVERIFY(!editing->snapshot().playing); QCOMPARE(editor->currentEdit().crop.width(), 160);
        QCOMPARE(digest(first), m_digest); window.close();
    }
    void updateIsOfferedThenFetchedCheckedAndPutInPlaceOnlyWhenAsked() {
        QTemporaryDir directory; const QDir install(directory.filePath("Clipsapp installé")), payload(directory.filePath("payload/Clipsapp"));
        const auto write = [](const QString& path, const QByteArray& content) { QDir().mkpath(QFileInfo(path).absolutePath()); QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(content) == content.size(); };
        const auto read = [](const QString& path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray("<absent>"); };
        QVERIFY(write(install.filePath("Clipsapp.exe"), "ancienne version")); QVERIFY(write(install.filePath("manifest.txt"), "Clipsapp.exe\nmanifest.txt\n"));
        QVERIFY(write(payload.filePath("Clipsapp.exe"), "nouvelle version")); QVERIFY(write(payload.filePath("platforms/qwindows.dll"), QByteArray(300000, 'q'))); QVERIFY(write(payload.filePath("manifest.txt"), "Clipsapp.exe\nplatforms/qwindows.dll\nmanifest.txt\n"));
        // The archive is made as scripts/release-windows.ps1 makes it, with the tar that Windows provides:
        // a name without a version, and one folder named Clipsapp that holds the application.
        const auto archive = directory.filePath("Clipsapp-windows-x64.zip"); QProcess tar; tar.setWorkingDirectory(directory.filePath("payload"));
        tar.start(QDir(qEnvironmentVariable("SystemRoot", "C:/Windows")).filePath("System32/tar.exe"), {"-a", "-c", "-f", QDir::toNativeSeparators(archive), "Clipsapp"}); QVERIFY(tar.waitForFinished(15000)); QCOMPARE(tar.exitCode(), 0);
        QFile packed(archive); QVERIFY(packed.open(QIODevice::ReadOnly)); const auto bytes = packed.readAll(); packed.close(); QVERIFY(bytes.size() > 100);
        const auto feed = [&](const QString& name, const QString& tag, const QByteArray& hash) {
            const QJsonObject asset{{"name", "Clipsapp-windows-x64.zip"}, {"state", "uploaded"}, {"size", bytes.size()}, {"digest", QStringLiteral("sha256:") + QString::fromLatin1(hash)}, {"browser_download_url", QUrl::fromLocalFile(archive).toString()}};
            const QJsonObject release{{"tag_name", tag}, {"name", "Clipsapp " + tag}, {"body", QStringLiteral("## Nouveautés\n\n- Ralenti à 0,5×\n- Capture d'une image")}, {"html_url", "https://github.com/ami/Clipsapp/releases"}, {"draft", false}, {"prerelease", false}, {"assets", QJsonArray{asset}}};
            const auto path = directory.filePath(name); return write(path, QJsonDocument(release).toJson()) ? QUrl::fromLocalFile(path) : QUrl();
        };
        const auto fingerprint = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
        using State = UpdateService::State;
        {
            // The same version, or an older one, is no update.
            UpdateService service("99.0.0", install.path()); service.setFeed(feed("same.json", "v99.0", fingerprint)); QSignalSpy current(&service, &UpdateService::upToDate), offered(&service, &UpdateService::available);
            QVERIFY(service.enabled()); service.check(true); QTRY_COMPARE_WITH_TIMEOUT(current.size(), 1, 5000); QCOMPARE(offered.size(), 0); QCOMPARE(service.state(), State::Idle);
            UpdateService silent("1.0", install.path()); QVERIFY(!silent.enabled()); silent.check(true); QCOMPARE(silent.state(), State::Idle);
        }
        {
            // An archive that is not the one published is thrown away: nothing is unpacked, nothing replaced.
            UpdateService service("0.8.0", install.path()); service.setFeed(feed("altered.json", "v99.0.0", QByteArray(64, '0'))); QSignalSpy offered(&service, &UpdateService::available);
            service.check(); QTRY_COMPARE_WITH_TIMEOUT(offered.size(), 1, 5000); service.download(); QTRY_VERIFY_WITH_TIMEOUT(!service.busy(), 15000);
            QCOMPARE(service.state(), State::Failed); QVERIFY(service.error().contains(QStringLiteral("publiée"))); QVERIFY(!QDir(install.filePath(".update/new")).exists()); QCOMPARE(read(install.filePath("Clipsapp.exe")), QByteArray("ancienne version"));
            QString error; QVERIFY(!service.install(error));
        }
        UpdateService service("0.8.0", install.path()); service.setFeed(feed("latest.json", "v99.0.0", fingerprint)); QSignalSpy offered(&service, &UpdateService::available);
        service.check(); QTRY_COMPARE_WITH_TIMEOUT(offered.size(), 1, 5000); QCOMPARE(service.state(), State::Available); QCOMPARE(service.info().version, QVersionNumber(99, 0, 0)); QCOMPARE(service.info().bytes, qint64(bytes.size()));
        // Found again at the next check, the same version is not announced twice; asked by hand, it is shown.
        service.check(); QTRY_COMPARE_WITH_TIMEOUT(service.state(), State::Available, 5000); QCOMPARE(offered.size(), 1); service.check(true); QTRY_COMPARE_WITH_TIMEOUT(offered.size(), 2, 5000);
        service.download(); QTRY_VERIFY_WITH_TIMEOUT(!service.busy(), 20000); QVERIFY2(service.state() == State::Ready, qPrintable(service.error())); QCOMPARE(service.receivedBytes(), qint64(bytes.size()));
        // Ready is not installed: the files in place are still those of the old version.
        QCOMPARE(read(install.filePath("Clipsapp.exe")), QByteArray("ancienne version")); QVERIFY(!QFile::exists(install.filePath("platforms/qwindows.dll")));
        QString error; QVERIFY2(service.install(error), qPrintable(error)); QCOMPARE(read(install.filePath("Clipsapp.exe")), QByteArray("nouvelle version")); QCOMPARE(read(install.filePath("platforms/qwindows.dll")).size(), 300000); QCOMPARE(service.state(), State::Idle);
        clearUpdateLeftovers(install.path()); QVERIFY(!QDir(install.filePath(".update")).exists());

        // In the window: a quiet button in the side bar, a notification, and a dialog that installs nothing by itself.
        ConfigStore store(directory.filePath("config")); auto config = AppConfig::defaults(); config.libraryPath = directory.filePath("clips"); QVERIFY(QDir().mkpath(config.libraryPath)); QVERIFY(store.save(config, error));
        MainWindow window(directory.filePath("config")); window.setAttribute(Qt::WA_DontShowOnScreen); window.show();
        auto* button = window.findChild<QPushButton*>("update-available"); auto* section = window.findChild<QAbstractButton*>("check-updates"); QVERIFY(button->isHidden()); QVERIFY(!section->isVisibleTo(&window));
        window.followUpdates({}, feed("window.json", "v99.0.0", fingerprint)); QTRY_VERIFY_WITH_TIMEOUT(!button->isHidden(), 5000); QCOMPARE(button->text(), QStringLiteral("Mise à jour 99.0.0")); QVERIFY(section->isChecked());
        window.setPage(MainWindow::Settings); window.findChild<SettingsPage*>()->showTab(2); QCoreApplication::processEvents(); QVERIFY(window.grab().save(previewPath("settings-updates.png")));
        QCOMPARE(window.findChild<QLabel*>("current-version")->text(), QStringLiteral("Version installée : %1").arg(CLIPSAPP_VERSION));
        bool seen = false;
        QTimer::singleShot(300, &window, [&] {
            auto* dialog = window.findChild<QDialog*>("update-dialog"); if (!dialog) return; seen = true;
            // The notes wait for the restart: here, only how to get the version.
            const bool complete = !dialog->findChild<QLabel*>("update-notes") &&dialog->findChild<QLabel*>("update-status")->text().contains(formatBytes(bytes.size())) && dialog->findChild<QPushButton*>("update-action")->text() == QStringLiteral("Télécharger") && dialog->findChild<QWidget*>("update-progress")->isHidden();
            seen = complete && dialog->grab().save(previewPath("update-dialog.png")); dialog->findChild<QPushButton*>("update-later")->click();
        });
        button->click(); QVERIFY(seen); QVERIFY(!window.relaunchRequested()); QVERIFY(!button->isHidden()); QCOMPARE(window.updates()->state(), State::Available);
        window.close();
    }
    void stepKeysAreTheSameTwoKeysOnEveryKeyboard() {
        // AZERTY names them A and E, QWERTY Q and E: the first and third keys of the top letter row.
        const auto azerty = stepKeysFor({'A', 'a'}, {'E', 'e'}); QCOMPARE(azerty.back, Qt::Key_A); QCOMPARE(azerty.forward, Qt::Key_E); QCOMPARE(azerty.backLabel, QString("A")); QCOMPARE(azerty.forwardLabel, QString("E"));
        const auto qwerty = stepKeysFor({'Q', 'q'}, {'E', 'e'}); QCOMPARE(qwerty.back, Qt::Key_Q); QCOMPARE(qwerty.forward, Qt::Key_E); QCOMPARE(qwerty.backLabel, QString("Q"));
        // Another alphabet: the Latin key answers, the letter printed on it is shown.
        const auto russian = stepKeysFor({'Q', 0x439}, {'E', 0x443}); QCOMPARE(russian.back, Qt::Key_Q); QCOMPARE(russian.forward, Qt::Key_E); QCOMPARE(russian.backLabel, QString(QChar(0x419))); QCOMPARE(russian.forwardLabel, QString(QChar(0x423)));
        // Dvorak has punctuation there; a dead key shows the key itself; an unknown layout falls back on Q and E.
        const auto dvorak = stepKeysFor({0xDE, '\''}, {0xBE, '.'}); QCOMPARE(dvorak.back, Qt::Key_Apostrophe); QCOMPARE(dvorak.forward, Qt::Key_Period); QCOMPARE(dvorak.backLabel, QString("'"));
        const auto dead = stepKeysFor({'A', 0x80000000u | '^'}, {'E', 'e'}); QCOMPARE(dead.back, Qt::Key_A); QCOMPARE(dead.backLabel, QString("A"));
        const auto unknown = stepKeysFor({}, {}); QCOMPARE(unknown.back, Qt::Key_Q); QCOMPARE(unknown.forward, Qt::Key_E);
        // On this machine, with the keyboard in use.
        const auto here = stepKeys(); QVERIFY(here.back != here.forward); QVERIFY(!here.backLabel.isEmpty() && !here.forwardLabel.isEmpty());
        qInfo().noquote() << "Step keys with this keyboard:" << here.backLabel << "/" << here.forwardLabel;
        QKeyEvent back(QEvent::KeyPress, here.back, Qt::NoModifier), forward(QEvent::KeyPress, here.forward, Qt::NoModifier), other(QEvent::KeyPress, Qt::Key_K, Qt::NoModifier), held(QEvent::KeyPress, here.back, Qt::ControlModifier);
        QCOMPARE(frameStep(&back), -1); QCOMPARE(frameStep(&forward), 1); QCOMPARE(frameStep(&other), 0); QCOMPARE(frameStep(&held), 0);
#ifdef Q_OS_WIN
        // A real key press carries its place on the keyboard, and that place decides.
        QKeyEvent first(QEvent::KeyPress, Qt::Key_Z, Qt::NoModifier, 0x10, 0, 0), third(QEvent::KeyPress, Qt::Key_Z, Qt::NoModifier, 0x12, 0, 0), elsewhere(QEvent::KeyPress, here.back, Qt::NoModifier, 0x2C, 0, 0);
        QCOMPARE(frameStep(&first), -1); QCOMPARE(frameStep(&third), 1); QCOMPARE(frameStep(&elsewhere), 0);
#endif
    }
    void replayStripWritesEveryStateInFull() {
        QWidget board; board.setAttribute(Qt::WA_DontShowOnScreen); board.resize(1220, 8 * 68 + 8); QList<ReplayStrip*> strips;
        const auto add = [&](const ReplayView& view) { auto* strip = new ReplayStrip(&board); strip->setGeometry(0, 8 + int(strips.size()) * 68, 1220, 60); strip->apply(view); strips.append(strip); return strip; };
        ReplayView view; view.profileSummary = QStringLiteral("Profil Principal · Écran 1 · 60 dernières secondes · 3 pistes"); view.liveSummary = QStringLiteral("Valorant détecté · 1080p 60 FPS · NVENC"); view.hotkey = "Ctrl+Alt+F10"; view.capacity = 60;
        const auto primary = [](ReplayStrip* strip) { return strip->findChild<QPushButton*>("replay-primary"); }; const auto secondary = [](ReplayStrip* strip) { return strip->findChild<QPushButton*>("replay-secondary"); };
        auto* idle = add(view); QCOMPARE(primary(idle)->text(), QStringLiteral("Démarrer le replay")); QVERIFY(primary(idle)->isEnabled()); QVERIFY(secondary(idle)->isHidden());
        view.state = ReplayView::Starting; auto* starting = add(view); QCOMPARE(primary(starting)->text(), QStringLiteral("Sauvegarder le clip")); QVERIFY(!primary(starting)->isEnabled()); QCOMPARE(secondary(starting)->text(), QStringLiteral("Annuler"));
        view.state = ReplayView::Running; view.canSave = true; view.seconds = 42.2; view.bytes = 137888563; auto* filling = add(view);
        QVERIFY(primary(filling)->isEnabled()); QCOMPARE(secondary(filling)->text(), QStringLiteral("Arrêter replay")); QCOMPARE(filling->findChild<QLabel*>("replay-seconds")->text(), QStringLiteral("42 s sur 60 s"));
        view.seconds = 60.4; view.bytes = 197132288; auto* full = add(view); QCOMPARE(full->findChild<QLabel*>("replay-seconds")->text(), QStringLiteral("60 s prêtes"));
        view.saving = true; auto* saving = add(view); QCOMPARE(primary(saving)->text(), QStringLiteral("Sauvegarde…")); QVERIFY(!primary(saving)->isEnabled());
        view.saving = false; view.warning = QStringLiteral("La capture de fenêtre ajoute un cadre jaune aux clips."); view.canFixWarning = true; auto* warned = add(view);
        QSignalSpy wholeScreen(warned, &ReplayStrip::fullScreenRequested); QVERIFY(!warned->findChild<QPushButton*>("replay-capture-screen")->isHidden()); warned->findChild<QPushButton*>("replay-capture-screen")->click(); QCOMPARE(wholeScreen.size(), 1);
        QVERIFY(full->findChild<QPushButton*>("replay-capture-screen")->isHidden());
        view = {}; view.state = ReplayView::Error; view.error = QStringLiteral("L'encodeur NVENC ne répond plus. Les clips déjà sauvegardés ne sont pas touchés."); auto* failed = add(view);
        QSignalSpy retried(failed, &ReplayStrip::startRequested), settings(failed, &ReplayStrip::settingsRequested); QCOMPARE(primary(failed)->text(), QStringLiteral("Réessayer")); primary(failed)->click(); secondary(failed)->click(); QCOMPARE(retried.size(), 1); QCOMPARE(settings.size(), 1);
        view = {}; view.state = ReplayView::Stopping; auto* stopping = add(view); QVERIFY(!primary(stopping)->isEnabled()); QVERIFY(secondary(stopping)->isHidden());
        // The save button asks for a save only while the replay runs; otherwise it starts it.
        QSignalSpy saved(full, &ReplayStrip::saveRequested), started(idle, &ReplayStrip::startRequested); primary(full)->click(); primary(idle)->click(); QCOMPARE(saved.size(), 1); QCOMPARE(started.size(), 1);
        board.show(); QTest::qWait(50); QVERIFY(board.grab().save(previewPath("replay-strip-states.png")));
    }
    void rapidSeeksKeepTheUiResponsiveAndTheLastRequestWins() {
        ClipPlayer player(nullptr, false); player.load(m_source); QTRY_VERIFY_WITH_TIMEOUT(player.snapshot().loaded, 3000);
        int ticks = 0; QTimer timer; timer.setInterval(5); connect(&timer, &QTimer::timeout, this, [&ticks] { ++ticks; }); timer.start();
        for (int i = 0; i < 16; ++i) { player.seek((i % 3) * 500); QTest::qWait(2); }
        player.seek(1700); QCOMPARE(player.snapshot().positionMs, qint64(1700));
        QTRY_VERIFY_WITH_TIMEOUT(!player.snapshot().loading, 3000); QCOMPARE(player.snapshot().positionMs, qint64(1700)); QVERIFY(player.snapshot().error.isEmpty()); QVERIFY(ticks > 0); player.close();
    }
    void previewsAndWhatWasReadOfEachClipAreKeptForTheNextLaunch() {
        QTemporaryDir directory; const auto storage = directory.filePath("cache"), key = ThumbnailCache::keyFor(m_source);
        const auto changing = directory.filePath("change.mp4"); QVERIFY(QFile::copy(m_source, changing)); const auto changingKey = ThumbnailCache::keyFor(changing);
        {
            ThumbnailCache cache; cache.setStorage(storage); QVERIFY(!cache.preview(key));
            cache.request(key, m_source); QTRY_VERIFY_WITH_TIMEOUT(cache.preview(key) && !cache.preview(key)->image.isNull(), 5000);
            cache.request(changingKey, changing); QTRY_VERIFY_WITH_TIMEOUT(cache.preview(changingKey), 5000);
        }
        QVERIFY(QFile::exists(QDir(storage).filePath("index.json"))); QCOMPARE(QDir(storage).entryList({"*.jpg"}, QDir::Files).size(), 2);
        // Known at once on the next launch, the clip is not read again: a request for it goes nowhere.
        { QFile file(changing); QVERIFY(file.open(QIODevice::Append)); file.write("plus long"); }
        ThumbnailCache again; again.setStorage(storage); QSignalSpy ready(&again, &ThumbnailCache::ready);
        QVERIFY(again.meta(key)); QCOMPARE(again.meta(key)->size, m_info.size); QCOMPARE(again.meta(key)->tracks, 3); QCOMPARE(again.meta(key)->durationMs, m_info.durationMs);
        const auto* preview = again.preview(key); QVERIFY(preview); QVERIFY(!preview->image.isNull()); QVERIFY(preview->image.width() <= 128);
        again.request(key, directory.filePath("absent.mp4")); QTest::qWait(300); QCOMPARE(ready.size(), 0);
        // A clip that changed since is forgotten, preview included; the next reading starts afresh.
        QTRY_VERIFY_WITH_TIMEOUT(!again.meta(changingKey), 3000); QVERIFY(!again.preview(changingKey));
        QTRY_COMPARE_WITH_TIMEOUT(QDir(storage).entryList({"*.jpg"}, QDir::Files).size(), 1, 3000);
    }
    void libraryFollowsWhatTheExplorerAddsRenamesAndRemoves() {
        QTemporaryDir directory; const auto library = directory.filePath("clips"); QVERIFY(QDir().mkpath(library + "/Jeu"));
        ConfigStore store(directory.filePath("config")); auto config = AppConfig::defaults(); config.libraryPath = library; QString error; QVERIFY(store.save(config, error));
        MainWindow window(directory.filePath("config")); window.setAttribute(Qt::WA_DontShowOnScreen); window.show();
        auto* list = window.findChild<ClipListView*>("clip-library"); auto* shares = window.findChild<ClipListView*>("discord-library"); auto* cache = window.findChild<ThumbnailCache*>(); QVERIFY(list); QVERIFY(shares); QVERIFY(cache);
        QTest::qWait(300); QVERIFY(clipPaths(list).isEmpty());
        // Copied by hand into a game folder, the clip shows without anything to refresh.
        const auto added = QDir(library).filePath("Jeu/Ajout.mp4"); QVERIFY(QFile::copy(m_source, added));
        QTRY_VERIFY_WITH_TIMEOUT(clipPaths(list).contains(added), 5000); QCOMPARE(list->clips()->index(list->rowOf(added), 0).data(ClipRole::Game).toString(), QString("Jeu"));
        QVERIFY(QDir().mkpath(library + "/Partage Discord")); const auto copy = QDir(library).filePath("Partage Discord/Ajout-1.discord.mp4"); QVERIFY(QFile::copy(m_source, copy));
        QTRY_VERIFY_WITH_TIMEOUT(clipPaths(shares).contains(copy), 5000); QVERIFY(!clipPaths(list).contains(copy));
        // Nothing reads the clips while the Explorer works on them: the folders are the user's.
        cache->setActive(false); QTRY_VERIFY_WITH_TIMEOUT(!cache->pending().isRunning(), 3000);
        QVERIFY(QDir(library).rename("Jeu", "Autre jeu")); const auto moved = QDir(library).filePath("Autre jeu/Ajout.mp4");
        QTRY_VERIFY_WITH_TIMEOUT(clipPaths(list).contains(moved) && !clipPaths(list).contains(added), 5000); QCOMPARE(list->clips()->index(list->rowOf(moved), 0).data(ClipRole::Game).toString(), QString("Autre jeu"));
        QVERIFY(QFile::remove(moved)); QVERIFY(QFile::remove(copy)); QTRY_VERIFY_WITH_TIMEOUT(clipPaths(list).isEmpty() && clipPaths(shares).isEmpty(), 5000);
        window.close();
    }
    void startupSettingsChooseWhenTheReplayStartsByItself() {
        QTemporaryDir directory; const auto configPath = directory.filePath("config"); QString error;
        { auto config = AppConfig::defaults(); config.libraryPath = directory.filePath("clips"); QVERIFY(ConfigStore(configPath).save(config, error)); }
        MainWindow window(configPath); window.setAttribute(Qt::WA_DontShowOnScreen); window.show(); window.setPage(MainWindow::Settings); window.findChild<QPushButton*>("settings-tab-2")->click();
        auto* settings = window.findChild<SettingsPage*>(); auto* mode = window.findChild<QComboBox*>("auto-replay"); auto* games = window.findChild<QWidget*>("replay-games"); auto* hint = window.findChild<QLabel*>("auto-replay-hint");
        QVERIFY(settings); QVERIFY(mode); QVERIFY(games); QVERIFY(hint);
        // Nothing starts by itself at first; a copy that does not manage the session has no switch for it.
        QCOMPARE(mode->currentData().toString(), QString("off")); QVERIFY(!games->isVisible()); QVERIFY(window.findChild<QCheckBox*>("login-start")->isHidden());
        mode->setCurrentIndex(mode->findData("launch")); QVERIFY(hint->text().contains(QStringLiteral("écran principal"))); QVERIFY(!games->isVisible());
        mode->setCurrentIndex(mode->findData("games")); QVERIFY(games->isVisible()); QVERIFY(hint->text().contains(QStringLiteral("30 secondes")));
        settings->addReplayGame("C:\\Jeux\\Valorant\\VALORANT.exe"); settings->addReplayGame("c:/jeux/valorant/valorant.exe"); settings->addReplayGame("D:/Steam/cs2.exe");
        QVERIFY(window.findChild<QWidget*>("replay-game-1")); QVERIFY(!window.findChild<QWidget*>("replay-game-2"));
        QScrollArea* area = nullptr; for (auto* parent = games->parentWidget(); parent && !area; parent = parent->parentWidget()) area = qobject_cast<QScrollArea*>(parent);
        QVERIFY(area); QTest::qWait(50); area->ensureWidgetVisible(window.findChild<QWidget*>("add-replay-game")); QTest::qWait(50);
        // Laid out, the section stacks its parts without overlap: the choice, its explanation, then the games.
        QVERIFY(mode->mapTo(area, QPoint(0, mode->height())).y() <= hint->mapTo(area, QPoint()).y()); QVERIFY(hint->mapTo(area, QPoint(0, hint->height())).y() <= games->mapTo(area, QPoint()).y());
        QVERIFY(window.grab().save(previewPath("settings-startup.png")));
        window.findChild<QPushButton*>("remove-replay-game-1")->click(); QTRY_VERIFY(!window.findChild<QWidget*>("replay-game-1"));
        QVERIFY(!window.findChild<QWidget*>("savebar")->isHidden()); window.findChild<QPushButton*>("settings-save")->click();
        AppConfig stored; QVERIFY(ConfigStore(configPath).load(stored, error)); QCOMPARE(stored.autoReplay, QString("games")); QCOMPARE(stored.replayGames, QStringList({"C:/Jeux/Valorant/VALORANT.exe"})); QVERIFY(!stored.launchAtLogin);
        // Cancelling brings back what was saved, list of games included.
        window.findChild<QPushButton*>("remove-replay-game-0")->click(); mode->setCurrentIndex(mode->findData("off"));
        window.findChild<QPushButton*>("settings-revert")->click(); QCOMPARE(mode->currentData().toString(), QString("games")); QTRY_VERIFY(window.findChild<QWidget*>("replay-game-0"));
        window.close();
    }
    void aWidgetRaisedAboveTheVideoGetsAWindowWithoutItsParents() {
        // A parent made native is what had Qt complain that a popup's parent "must be a top level window".
        QWidget top; top.setProperty("directVideo", true); top.setAttribute(Qt::WA_DontShowOnScreen); top.resize(320, 200);
        auto* pages = new QStackedWidget(&top); auto* page = new QWidget; pages->addWidget(page); auto* toast = new QLabel(QStringLiteral("Clip sauvegardé"), page);
        top.show(); QCoreApplication::processEvents();
        VideoSurface::raiseAbove(toast);
        QVERIFY(toast->internalWinId()); QVERIFY(!page->internalWinId()); QVERIFY(!pages->internalWinId());
    }
    void theCropIsDrawnWithTheMouseOverTheVideoShownByTheCard() {
        // Shown by the graphics card, the image lies in a window of its own: Windows hands it the mouse, not the view.
        QWidget top; top.resize(640, 400); auto* column = new QVBoxLayout(&top); column->setContentsMargins(40, 60, 40, 20);
        auto* canvas = new VideoCanvas; column->addWidget(canvas);
        top.show(); QVERIFY(QTest::qWaitForWindowExposed(&top));
        auto* surface = canvas->prepareSurface(); if (!surface) QSKIP("This machine shows no clip through its graphics card.");
        QImage image(320, 192, QImage::Format_ARGB32); image.fill(Qt::red);
        canvas->setFrame(image, image.size()); canvas->setCrop(image.rect()); canvas->setCropTool(true); canvas->setDirect(true); QTest::qWait(50);
        QVERIFY(surface->isVisible()); QVERIFY(surface->windowHandle());
        QSignalSpy selected(canvas, &VideoCanvas::cropSelected);
        // Pressed, dragged and let go over the surface, the way the system sends it.
        const auto at = [surface, canvas](QPoint inCanvas) { return surface->mapFrom(canvas, inCanvas); };
        const QPoint from(canvas->width() / 4, canvas->height() / 4), to(canvas->width() * 3 / 4, canvas->height() * 3 / 4);
        QTest::mousePress(surface->windowHandle(), Qt::LeftButton, Qt::NoModifier, at(from)); QTest::mouseMove(surface->windowHandle(), at((from + to) / 2)); QTest::mouseMove(surface->windowHandle(), at(to));
        QTest::mouseRelease(surface->windowHandle(), Qt::LeftButton, Qt::NoModifier, at(to));
        QVERIFY(!selected.isEmpty()); const auto crop = selected.last().first().toRect(); QVERIFY(crop.width() > 60 && crop.width() < 260); QVERIFY(crop.height() > 30);
    }
    void theEditorDrawsAFreeCropWithTheMouseWhereverTheImageIsShown() {
        ClipEditor editor; editor.resize(1100, 760); editor.move(40, 40); editor.show(); QVERIFY(QTest::qWaitForWindowExposed(&editor)); editor.openClip(m_source);
        // Windows answers with what is under the mouse on screen: the editor comes first, above anything else open.
        editor.raise(); editor.activateWindow(); (void)QTest::qWaitForWindowActive(&editor);
        auto* width = editor.findChild<QSpinBox*>("crop-width"); auto* height = editor.findChild<QSpinBox*>("crop-height"); QVERIFY(width); QVERIFY(height);
        QTRY_VERIFY_WITH_TIMEOUT(width->isEnabled() && width->value() == 320, 5000); QTest::qWait(300);
        auto* canvas = editor.findChild<VideoCanvas*>(); QVERIFY(canvas);
        editor.findChild<QPushButton*>("crop-ratio-4")->click(); QTest::qWait(100);
        // The mouse goes to the window Windows finds under it, the card's surface when it shows the image.
        const auto under = [canvas](QPoint inCanvas) {
            const auto global = canvas->mapToGlobal(inCanvas); const double ratio = canvas->devicePixelRatioF();
            auto* widget = QWidget::find(WId(WindowFromPoint(POINT{LONG(global.x() * ratio), LONG(global.y() * ratio)})));
            return std::pair<QWindow*, QPoint>(widget ? widget->windowHandle() : nullptr, widget ? widget->mapFromGlobal(global) : QPoint());
        };
        const QPoint from(canvas->width() * 3 / 10, canvas->height() * 3 / 10), to(canvas->width() * 6 / 10, canvas->height() * 6 / 10);
        const auto start = under(from), middle = under((from + to) / 2), end = under(to);
        if (!start.first || !middle.first || !end.first) QSKIP("Another window covers the editor on this screen: Windows keeps it in front of the tests.");
        QTest::mousePress(start.first, Qt::LeftButton, Qt::NoModifier, start.second); QTest::mouseMove(middle.first, middle.second); QTest::mouseMove(end.first, end.second);
        QTest::mouseRelease(end.first, Qt::LeftButton, Qt::NoModifier, end.second);
        const auto describe = [&] {
            auto* widget = QWidget::find(WId(WindowFromPoint([&] { const auto g = canvas->mapToGlobal(from); return POINT{LONG(g.x() * canvas->devicePixelRatioF()), LONG(g.y() * canvas->devicePixelRatioF())}; }())));
            return QStringLiteral("under the mouse: %1 '%2' · surface %3 visible %4").arg(widget ? widget->metaObject()->className() : "nothing", widget ? widget->objectName() : QString()).arg(canvas->surface() != nullptr).arg(canvas->surface() && canvas->surface()->isVisible());
        };
        QTRY_VERIFY2_WITH_TIMEOUT(width->value() < 300, qPrintable(describe()), 2000); QVERIFY(width->value() > 40); QVERIFY(height->value() < 180);
        editor.suspend();
    }
    void screenshotsHaveTheirPageAndTheRailShowsTheGamesOfWhatIsOnScreen() {
        QTemporaryDir directory; const auto library = directory.filePath("clips"); QVERIFY(QDir().mkpath(library + "/ARC Raiders"));
        QVERIFY(QFile::copy(m_source, library + "/ARC Raiders/clip.mp4"));
        QImage picture(160, 90, QImage::Format_RGB32); picture.fill(Qt::darkCyan);
        for (const auto& name : {"/Screenshots/ARC Raiders/vue.png", "/Screenshots/EA SPORTS FC 27/but.png"}) { QVERIFY(QDir().mkpath(QFileInfo(library + name).absolutePath())); QVERIFY(picture.save(library + name)); }
        { auto config = AppConfig::defaults(); config.libraryPath = library; QString error; QVERIFY(ConfigStore(directory.filePath("config")).save(config, error)); }
        MainWindow window(directory.filePath("config")); window.setAttribute(Qt::WA_DontShowOnScreen); window.show();
        auto* clips = window.findChild<ClipListView*>("clip-library"); auto* shots = window.findChild<ClipListView*>("screenshot-library"); QVERIFY(clips); QVERIFY(shots);
        QTRY_COMPARE_WITH_TIMEOUT(clipPaths(clips).size(), 1, 5000); QTRY_COMPARE_WITH_TIMEOUT(clipPaths(shots).size(), 2, 5000);
        const auto height = [&window](const char* name) { return window.findChild<QScrollArea*>(name)->maximumHeight(); };
        // On the clips, their games; those of the screenshots wait for their page.
        QVERIFY(height("nav-games") > 0); QCOMPARE(height("nav-screenshot-games"), 0);
        window.findChild<QPushButton*>("nav-screenshots")->click(); QVERIFY(window.findChild<QWidget*>("screenshots-page")->isVisible());
        QCOMPARE(height("nav-games"), 0); QCOMPARE(height("nav-screenshot-games"), 3 * 31 + 3);
        QVERIFY(window.grab().save(previewPath("screenshots.png")));
        // A game of the screenshots filters their list.
        // New rows show on the next turn of the event loop.
        QPushButton* fc = nullptr;
        QTRY_VERIFY((fc = [&window]() -> QPushButton* { for (auto* button : window.findChild<QScrollArea*>("nav-screenshot-games")->findChildren<QPushButton*>()) if (button->property("game").toString() == "EA SPORTS FC 27" && button->isVisible()) return button; return nullptr; }())); fc->click(); QCOMPARE(clipPaths(shots), QStringList({library + "/Screenshots/EA SPORTS FC 27/but.png"}));
        window.findChild<QPushButton*>("nav-settings")->click(); QCOMPARE(height("nav-games"), 0); QCOMPARE(height("nav-screenshot-games"), 0);
        window.findChild<QPushButton*>("nav-clips")->click(); QVERIFY(height("nav-games") > 0); QCOMPARE(height("nav-screenshot-games"), 0);
        window.close();
    }
    void theScreenshotEditorSavesACroppedCopyAndKeepsTheOriginal() {
        QTemporaryDir directory; const auto original = directory.filePath("ARC Raiders-vue.png");
        QImage picture(400, 200, QImage::Format_RGB32); picture.fill(Qt::red); for (int y = 0; y < 200; ++y) for (int x = 200; x < 400; ++x) picture.setPixelColor(x, y, Qt::blue);
        QVERIFY(picture.save(original)); const auto digestBefore = digest(original);
        ScreenshotEditor editor; editor.setAttribute(Qt::WA_DontShowOnScreen); editor.resize(1100, 700); editor.show(); editor.openImage(original);
        QCOMPARE(editor.imageSize(), QSize(400, 200)); QTRY_VERIFY(editor.findChild<QLabel*>("screenshot-title")->width() > 60);
        auto* save = editor.findChild<QPushButton*>("screenshot-save"); QVERIFY(!save->isEnabled());
        // A square in the middle of the picture: half red, half blue.
        editor.findChild<QPushButton*>("screenshot-ratio-3")->click(); QCOMPARE(editor.crop(), QRect(100, 0, 200, 200)); QVERIFY(save->isEnabled());
        QTest::qWait(50); QVERIFY(editor.grab().save(previewPath("screenshot-editor.png")));
        QSignalSpy saved(&editor, &ScreenshotEditor::saved); save->click();
        QTRY_COMPARE_WITH_TIMEOUT(saved.size(), 1, 5000); const auto copy = saved.first().first().toString();
        QCOMPARE(copy, directory.filePath("ARC Raiders-vue (recadré).png")); QVERIFY(saved.first().at(1).toString() == original);
        const QImage cropped(copy); QCOMPARE(cropped.size(), QSize(200, 200)); QCOMPARE(cropped.pixelColor(50, 100), QColor(Qt::red)); QCOMPARE(cropped.pixelColor(150, 100), QColor(Qt::blue));
        QCOMPARE(digest(original), digestBefore); QVERIFY(editor.findChild<QWidget*>("screenshot-banner")->isVisible());
        QCOMPARE(ScreenshotEditor::croppedPath(original), directory.filePath("ARC Raiders-vue (recadré 2).png"));
    }
    void theClipEditorKeepsTheFrameShownAmongTheScreenshots() {
        QTemporaryDir directory; const auto library = directory.filePath("clips"); QVERIFY(QDir().mkpath(library + "/ARC Raiders")); const auto clip = library + "/ARC Raiders/clip.mp4"; QVERIFY(QFile::copy(m_source, clip));
        { auto config = AppConfig::defaults(); config.libraryPath = library; QString error; QVERIFY(ConfigStore(directory.filePath("config")).save(config, error)); }
        MainWindow window(directory.filePath("config")); window.setAttribute(Qt::WA_DontShowOnScreen); window.show();
        auto* list = window.findChild<ClipListView*>("clip-library"); QTRY_VERIFY_WITH_TIMEOUT(clipPaths(list).contains(clip), 5000); emit list->openRequested(clip);
        auto* width = window.findChild<QSpinBox*>("crop-width"); QTRY_VERIFY_WITH_TIMEOUT(width->isEnabled() && width->value() == 320, 5000);
        window.findChild<QPushButton*>("editor-screenshot")->click();
        // Taken at the size of the clip, filed with its game, listed among the screenshots.
        const auto folder = library + "/Screenshots/ARC Raiders";
        QTRY_COMPARE_WITH_TIMEOUT(QDir(folder).entryList({"*.png"}, QDir::Files).size(), 1, 5000);
        // The file is listed as soon as it is created: it is read once it is written.
        const auto takenPath = QDir(folder).filePath(QDir(folder).entryList({"*.png"}, QDir::Files).first());
        QTRY_COMPARE_WITH_TIMEOUT(QImage(takenPath).size(), QSize(320, 192), 5000);
        auto* shots = window.findChild<ClipListView*>("screenshot-library"); QTRY_COMPARE_WITH_TIMEOUT(clipPaths(shots).size(), 1, 5000);
        // With a framing set, only the framed part is kept.
        width->setValue(160); window.findChild<QSpinBox*>("crop-height")->setValue(96); window.findChild<QPushButton*>("editor-screenshot")->click();
        QTRY_COMPARE_WITH_TIMEOUT(QDir(folder).entryList({"*.png"}, QDir::Files).size(), 2, 5000);
        bool framed = false; for (const auto& name : QDir(folder).entryList({"*.png"}, QDir::Files)) framed |= QImage(QDir(folder).filePath(name)).size() == QSize(160, 96);
        QVERIFY(framed); window.close();
    }
    void aGameKeepsItsOwnVolumesAndTheOthersKeepTheUsualOnes() {
        QTemporaryDir directory; const auto library = directory.filePath("clips");
        const auto arc = library + "/ARC Raiders/arc.mp4", fc = library + "/EA SPORTS FC 27/fc.mp4";
        for (const auto& clip : {arc, fc}) { QVERIFY(QDir().mkpath(QFileInfo(clip).absolutePath())); QVERIFY(QFile::copy(m_source, clip)); }
        const auto configPath = directory.filePath("config"); { auto config = AppConfig::defaults(); config.libraryPath = library; QString error; QVERIFY(ConfigStore(configPath).save(config, error)); }
        MainWindow window(configPath); window.setAttribute(Qt::WA_DontShowOnScreen); window.show();
        auto* list = window.findChild<ClipListView*>("clip-library"); QTRY_COMPARE_WITH_TIMEOUT(clipPaths(list).size(), 2, 5000);
        const auto open = [&](const QString& clip) -> QSlider* {
            emit list->openRequested(clip);
            if (!QTest::qWaitFor([&] { auto* width = window.findChild<QSpinBox*>("crop-width"); return width && width->isEnabled() && width->value() == 320 && window.findChild<QSlider*>("track-volume-1"); }, 5000)) return nullptr;
            QTest::qWait(50); return window.findChild<QSlider*>("track-volume-1");
        };
        auto* game = open(arc); QVERIFY(game); QCOMPARE(game->value(), 100); game->setValue(40);
        auto* keep = window.findChild<QPushButton*>("keep-mix"); QTRY_VERIFY(keep->isVisible()); QCOMPARE(keep->text(), QStringLiteral("Garder ces volumes pour ARC Raiders")); keep->click();
        AppConfig stored; QString error; QVERIFY(ConfigStore(configPath).load(stored, error));
        QCOMPARE(stored.gameMixes.value("ARC Raiders").value("jeu"), 0.4); QVERIFY(stored.defaultMix.isEmpty());
        // The clips of another game open as before; those of the game, with its levels.
        window.findChild<QPushButton*>("editor-back")->click(); auto* other = open(fc); QVERIFY(other); QCOMPARE(other->value(), 100);
        window.findChild<QPushButton*>("editor-back")->click(); auto* again = open(arc); QVERIFY(again); QCOMPARE(again->value(), 40);
        // The settings list it, and forgetting it brings the usual levels back.
        window.findChild<QPushButton*>("editor-back")->click(); window.setPage(MainWindow::Settings); window.findChild<QPushButton*>("settings-tab-1")->click();
        QTRY_VERIFY(window.findChild<QWidget*>("game-mix-0")); QVERIFY(window.findChild<QWidget*>("game-mixes")->isVisible());
        window.findChild<QPushButton*>("forget-game-mix-0")->click(); window.findChild<QPushButton*>("settings-save")->click();
        QVERIFY(ConfigStore(configPath).load(stored, error)); QVERIFY(stored.gameMixes.isEmpty());
        window.close();
    }
    void aWindowIsCapturedPixelForPixel() {
        // A window of the test itself: nothing of the screen of whoever runs the tests is read.
        QWidget target; target.setWindowTitle(QStringLiteral("Clipsapp, capture d'essai")); target.setAutoFillBackground(true);
        QPalette palette = target.palette(); palette.setColor(QPalette::Window, Qt::red); target.setPalette(palette); target.resize(300, 200); target.show(); QVERIFY(QTest::qWaitForWindowExposed(&target)); QTest::qWait(200);
        QString error; const auto image = captureScreenImage(QStringLiteral("window:%1|%2").arg(QCoreApplication::applicationFilePath().toCaseFolded(), target.windowTitle()), false, error);
        QVERIFY2(!image.isNull(), qPrintable(error));
        QVERIFY(image.width() >= qRound(300 * target.devicePixelRatioF())); QVERIFY(image.height() >= qRound(200 * target.devicePixelRatioF()));
        const auto middle = image.pixelColor(image.width() / 2, image.height() / 2); QVERIFY2(middle.red() > 200 && middle.green() < 60 && middle.blue() < 60, qPrintable(middle.name()));
    }
    void screenshotSettingsTakeAShortcutAndASoundOfTheirOwn() {
        QTemporaryDir directory; const auto configPath = directory.filePath("config"); QString error;
        { auto config = AppConfig::defaults(); config.libraryPath = directory.filePath("clips"); QVERIFY(ConfigStore(configPath).save(config, error)); }
        MainWindow window(configPath); window.setAttribute(Qt::WA_DontShowOnScreen); window.show(); window.setPage(MainWindow::Settings);
        auto* settings = window.findChild<SettingsPage*>(); settings->showTab(0); QCoreApplication::processEvents();
        auto* key = window.findChild<QKeySequenceEdit*>("screenshot-hotkey"); QVERIFY(key); QCOMPARE(key->keySequence(), QKeySequence(QStringLiteral("Ctrl+Alt+F11")));
        QVERIFY(window.findChild<QPushButton*>("screenshot-hotkey-change")->isVisible());
        key->setKeySequence(QKeySequence(QStringLiteral("Ctrl+Shift+S")));
        settings->showTab(2); QCoreApplication::processEvents();
        auto* sound = window.findChild<QComboBox*>("screenshot-sound-choice"); QVERIFY(sound && sound->isVisible()); QCOMPARE(sound->currentData().toString(), QString("shutter"));
        sound->setCurrentIndex(sound->findData("coin")); window.findChild<QSlider*>("screenshot-sound-volume")->setValue(70);
        window.findChild<QPushButton*>("settings-save")->click();
        AppConfig stored; QVERIFY(ConfigStore(configPath).load(stored, error));
        QCOMPARE(stored.screenshotHotkey, QString("Ctrl+Shift+S")); QCOMPARE(stored.screenshotSoundId, QString("coin")); QCOMPARE(stored.screenshotSoundVolume, 70);
        window.close();
    }
    void whatsNewShowsTheNotesOfTheInstalledVersionOverTheWindow() {
        const auto sample = QStringLiteral("## 0.8.3\r\n\r\n**Nouveautés**\r\n\r\n- **Screenshots** : un raccourci prend l'écran en qualité native.\r\n- **Démarrage automatique**, dans **Réglages → Général** :\r\n  - **Lancer Clipsapp** avec Windows.\r\n  - Replay automatique.\r\n\r\n**Corrections**\r\n\r\n- Le recadrage `Libre` refonctionne, voir [la page](https://example.com/a).\r\n");
        const auto sections = parseReleaseNotes(sample);
        QCOMPARE(sections.size(), 2); QCOMPARE(sections[0].title, QStringLiteral("Nouveautés")); QCOMPARE(sections[0].items.size(), 2);
        QCOMPARE(sections[0].items[0].lead, QStringLiteral("Screenshots")); QCOMPARE(sections[0].items[0].text, QStringLiteral("Un raccourci prend l'écran en qualité native."));
        QVERIFY(sections[0].items[1].lead.isEmpty()); QCOMPARE(sections[0].items[1].details, QStringList({QStringLiteral("**Lancer Clipsapp** avec Windows."), QStringLiteral("Replay automatique.")}));
        QCOMPARE(sections[1].title, QStringLiteral("Corrections")); QCOMPARE(sections[1].items.size(), 1);
        const auto html = inlineMarkdown(sections[1].items[0].text + QStringLiteral(" <b>"));
        QVERIFY(html.contains(QStringLiteral("Consolas"))); QVERIFY(html.contains(QStringLiteral("href=\"https://example.com/a\""))); QVERIFY(html.contains(QStringLiteral("&lt;b&gt;")));

        // In the window, over a library.
        QTemporaryDir directory; const auto library = directory.filePath("clips");
        for (const auto& [game, name] : std::initializer_list<std::pair<const char*, const char*>>{{"Nova Strike", "Clutch 1v3"}, {"Turbo Ball", "Arrêt décisif"}, {"Nova Strike", "Ace en défense"}}) {
            QVERIFY(QDir().mkpath(library + '/' + game)); QVERIFY(QFile::copy(m_source, QStringLiteral("%1/%2/%3.mp4").arg(library, QString::fromUtf8(game), QString::fromUtf8(name))));
        }
        ConfigStore store(directory.filePath("config")); auto config = AppConfig::defaults(); config.libraryPath = library; QString error; QVERIFY(store.save(config, error));
        MainWindow window(directory.filePath("config")); window.setAttribute(Qt::WA_DontShowOnScreen); window.show();
        auto* list = window.findChild<ClipListView*>("clip-library"); QTRY_COMPARE_WITH_TIMEOUT(clipPaths(list).size(), 3, 5000); QTest::qWait(1500);
        // The journal built into the program: newest first, the version in use among them, every version readable.
        const auto releases = bundledChangelog(); QVERIFY(releases.size() >= 4); QCOMPARE(releases.first().version, QStringLiteral(CLIPSAPP_VERSION)); QCOMPARE(releases.last().version, QStringLiteral("0.8.0"));
        for (const auto& release : releases) QVERIFY2(!parseReleaseNotes(release.notes).isEmpty(), qPrintable(release.version));
        // A new feature ends on where it is found: the place is given apart, shown under it.
        const auto features = parseReleaseNotes(releases.first().notes).first(); QCOMPARE(features.title, QStringLiteral("Nouveautés"));
        for (const auto& item : features.items) { QVERIFY2(item.where.startsWith(QStringLiteral("Réglages → ")), qPrintable(item.lead + QStringLiteral(" | where=") + item.where + QStringLiteral(" | text=") + item.text)); QVERIFY(!item.text.contains(QStringLiteral("Réglages →"))); }
        QCOMPARE(features.items[1].lead, QStringLiteral("Thèmes")); QCOMPARE(features.items[1].where, QStringLiteral("Réglages → Général → Thèmes"));
        QCOMPARE(changelogReleases(sample).size(), 1); QCOMPARE(changelogReleases(sample).first().version, QStringLiteral("0.8.3"));
        const auto composite = [&](QWidget* dialog) { QImage shot = window.grab().toImage(); QPainter painter(&shot); painter.drawPixmap(dialog->mapToGlobal(QPoint()) - window.mapToGlobal(QPoint()), dialog->grab()); return shot; };
        const auto hasText = [](QWidget* widget, const QString& text) { for (auto* found : widget->findChildren<QLabel*>()) if (found->text() == text) return true; return false; };

        // After an update: the notes of the version installed, alone, once the window is on screen.
        bool after = false;
        QTimer::singleShot(1400, &window, [&] {
            auto* dialog = static_cast<WhatsNewDialog*>(window.findChild<QDialog*>("whats-new")); if (!dialog) return;
            after = dialog->isVisible() && !dialog->findChild<QComboBox*>("whats-new-versions") && hasText(dialog, QStringLiteral("Quoi de neuf dans Clipsapp %1").arg(CLIPSAPP_VERSION))
                && hasText(dialog, QStringLiteral("Mise à jour installée, à la place de la version 0.8.2.")) && dialog->height() < window.height() && dialog->findChild<QWidget*>("whats-new-notes");
            after = after && composite(dialog).save(previewPath("whats-new.png"));
            auto* scroll = dialog->findChild<QScrollArea*>("whats-new-scroll"); scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum()); QTest::qWait(50);
            after = after && composite(dialog).save(previewPath("whats-new-end.png"));
            dialog->findChild<QPushButton*>("whats-new-close")->click();
        });
        window.announceUpdated(QStringLiteral("0.8.2")); QTRY_VERIFY_WITH_TIMEOUT(after, 5000);

        // The version in the side bar opens the journal on the version in use; the others are a choice away, in a card that keeps its height.
        bool journal = false;
        QTimer::singleShot(300, &window, [&] {
            auto* dialog = static_cast<WhatsNewDialog*>(window.findChild<QDialog*>("whats-new")); if (!dialog) return;
            auto* versions = dialog->findChild<QComboBox*>("whats-new-versions"); if (!versions) return;
            const bool opened = versions->count() == releases.size() && versions->currentIndex() == 0 && versions->itemText(0).endsWith(QStringLiteral("installée")) && dialog->shown() == 0 && hasText(dialog, QStringLiteral("C'est la version que vous utilisez."));
            const int height = dialog->height(); versions->setCurrentIndex(int(releases.size()) - 1); QTest::qWait(50);
            journal = opened && dialog->shown() == releases.size() - 1 && hasText(dialog, QStringLiteral("Quoi de neuf dans Clipsapp 0.8.0")) && hasText(dialog, QStringLiteral("Une version précédente. Vous utilisez la %1.").arg(CLIPSAPP_VERSION)) && dialog->height() == height;
            versions->setCurrentIndex(1); QTest::qWait(50); journal = journal && composite(dialog).save(previewPath("whats-new-journal.png"));
            dialog->findChild<QPushButton*>("whats-new-close")->click();
        });
        auto* version = window.findChild<QPushButton*>("app-version"); QCOMPARE(version->text(), QStringLiteral("Version %1").arg(CLIPSAPP_VERSION)); version->click(); QVERIFY(journal);
        window.close();
    }
    void screenshotsAreSeenFullScreenOneAfterTheOtherWithTheArrows() {
        QTemporaryDir directory; QStringList paths;
        for (const auto& [name, color] : std::initializer_list<std::pair<const char*, QColor>>{{"un.png", Qt::red}, {"deux.png", Qt::green}, {"trois.png", Qt::blue}}) {
            QImage picture(320, 180, QImage::Format_RGB32); picture.fill(color); const auto path = directory.filePath(name); QVERIFY(picture.save(path)); paths.append(path);
        }
        QWidget owner; ImageViewer viewer(&owner); viewer.setAttribute(Qt::WA_DontShowOnScreen); viewer.resize(800, 500); QSignalSpy closed(&viewer, &ImageViewer::closed);
        viewer.open(paths, 0); QCOMPARE(viewer.currentPath(), paths[0]); QCOMPARE(viewer.image().pixelColor(10, 10), QColor(Qt::red));
        QVERIFY(!viewer.findChild<QPushButton*>("image-viewer-previous")->isEnabled()); QVERIFY(viewer.findChild<QPushButton*>("image-viewer-next")->isEnabled());
        QTest::keyClick(&viewer, Qt::Key_Right); QCOMPARE(viewer.index(), 1); QCOMPARE(viewer.image().pixelColor(10, 10), QColor(Qt::green));
        QTest::keyClick(&viewer, Qt::Key_Right); QTest::keyClick(&viewer, Qt::Key_Right); QCOMPARE(viewer.index(), 2);
        QTest::keyClick(&viewer, Qt::Key_Left); QCOMPARE(viewer.index(), 1); QTest::keyClick(&viewer, Qt::Key_Home); QCOMPARE(viewer.index(), 0); QTest::keyClick(&viewer, Qt::Key_Left); QCOMPARE(viewer.index(), 0);
        QTest::keyClick(&viewer, Qt::Key_End); QCOMPARE(viewer.index(), 2); QCOMPARE(viewer.findChild<QLabel*>("image-viewer-count")->text(), QStringLiteral("3 sur 3"));
        QTest::qWait(50); QVERIFY(viewer.grab().save(previewPath("image-viewer.png")));
        QTest::keyClick(&viewer, Qt::Key_Escape); QCOMPARE(closed.size(), 1); QCOMPARE(closed.first().first().toString(), paths[2]); QVERIFY(viewer.isHidden());
        // From the list of screenshots: a picture is not played, it is seen larger, in the order of the list.
        LibraryPage page(nullptr, LibraryPage::Screenshots); QSignalSpy view(&page, &LibraryPage::viewRequested), open(&page, &LibraryPage::openRequested);
        QList<LibraryItem> items; for (const auto& path : paths) items.append({ClipEntry{path, QFileInfo(path).completeBaseName(), QStringLiteral("Jeu"), QStringLiteral("PNG"), 10, QDateTime::currentDateTime()}});
        page.setItems(items, 20000000); emit page.list()->actionRequested(ClipRowDelegate::Play, paths[1], {});
        QCOMPARE(view.size(), 1); QCOMPARE(view.first().at(0).toStringList().size(), 3); QCOMPARE(open.size(), 0);
    }
    void marksDrawnOnAScreenshotLandInTheFullPicture() {
        QTemporaryDir directory; const auto original = directory.filePath("vue.png");
        QImage picture(400, 200, QImage::Format_RGB32); picture.fill(Qt::white); QVERIFY(picture.save(original)); const auto before = digest(original);
        ScreenshotEditor editor; editor.setAttribute(Qt::WA_DontShowOnScreen); editor.resize(1100, 700); editor.show(); editor.openImage(original); QTest::qWait(50);
        auto* save = editor.findChild<QPushButton*>("screenshot-save"); QVERIFY(!save->isEnabled());
        // Choosing a tool turns to drawing; a frame is drawn across the middle of the picture, in red.
        editor.findChild<QPushButton*>("markup-tool-2")->click(); QCOMPARE(editor.findChild<Segmented*>("screenshot-mode")->currentIndex(), 1);
        auto* canvas = editor.findChild<VideoCanvas*>("screenshot-canvas"); auto* layer = editor.findChild<MarkupLayer*>(); QVERIFY(canvas); QVERIFY(layer);
        const auto area = canvas->imageArea(); const auto at = [&area](double x, double y) { return QPointF(area.x() + x * area.width() / 400, area.y() + y * area.height() / 200).toPoint(); };
        QTest::mousePress(layer, Qt::LeftButton, Qt::NoModifier, at(100, 50)); QTest::mouseMove(layer, at(200, 100)); QTest::mouseMove(layer, at(300, 150)); QTest::mouseRelease(layer, Qt::LeftButton, Qt::NoModifier, at(300, 150));
        QCOMPARE(editor.marks().size(), 1); QCOMPARE(editor.marks().first().kind, Mark::Rectangle); QVERIFY(save->isEnabled());
        QTest::qWait(50); QVERIFY(editor.grab().save(previewPath("screenshot-markup.png")));
        // Undone, then drawn again.
        editor.findChild<QPushButton*>("markup-undo")->click(); QVERIFY(editor.marks().isEmpty()); QVERIFY(!save->isEnabled());
        QTest::mousePress(layer, Qt::LeftButton, Qt::NoModifier, at(100, 50)); QTest::mouseMove(layer, at(300, 150)); QTest::mouseRelease(layer, Qt::LeftButton, Qt::NoModifier, at(300, 150));
        QSignalSpy saved(&editor, &ScreenshotEditor::saved); save->click(); QTRY_COMPARE_WITH_TIMEOUT(saved.size(), 1, 5000);
        const QImage copy(saved.first().first().toString()); QCOMPARE(saved.first().first().toString(), directory.filePath("vue (annoté).png")); QCOMPARE(copy.size(), QSize(400, 200));
        const auto edge = copy.pixelColor(100, 100); QVERIFY2(edge.red() > 200 && edge.green() < 90, qPrintable(edge.name())); QCOMPARE(copy.pixelColor(200, 100), QColor(Qt::white)); QCOMPARE(copy.pixelColor(20, 20), QColor(Qt::white));
        QCOMPARE(digest(original), before); QVERIFY(editor.marks().isEmpty());
    }
    void widgetsAboveTheVideoFollowTheLayoutWhenItMoves() {
        // The editor's buttons over the image, raised above the video, while a banner appears above the canvas.
        QWidget top; top.setProperty("directVideo", true); top.resize(640, 420); auto* column = new QVBoxLayout(&top);
        auto* banner = new QLabel(QStringLiteral("Clip exporté")); banner->setFixedHeight(80); banner->hide(); column->addWidget(banner);
        auto* canvas = new QWidget; column->addWidget(canvas, 1); auto* overlay = new QPushButton(QStringLiteral("Résultat"), canvas); overlay->move(20, 20);
        top.show(); QVERIFY(QTest::qWaitForWindowExposed(&top)); VideoSurface::raiseAbove(overlay); QCoreApplication::processEvents();
        // Where Windows really shows it, against where the layout puts it, both in pixels of the window.
        const auto placed = [overlay, &top] { RECT rect{}; GetWindowRect(HWND(overlay->winId()), &rect); POINT origin{0, 0}; ClientToScreen(HWND(top.winId()), &origin); return QPoint(rect.left - origin.x, rect.top - origin.y); };
        const auto expected = [overlay] { return (QPointF(overlay->mapTo(overlay->window(), QPoint())) * overlay->devicePixelRatioF()).toPoint(); };
        QCOMPARE(placed(), expected());
        banner->show(); QTest::qWait(50);
        QCOMPARE(placed(), expected());
        banner->hide(); top.resize(720, 380); QTest::qWait(50);
        QCOMPARE(placed(), expected());
        // Placing its window does not move the widget in its own parent.
        QCOMPARE(overlay->pos(), QPoint(20, 20));
    }
    void savedClipSoundsAreDistinctAndFollowTheirVolume() {
        const auto sounds = clipSavedSounds(); QVERIFY(sounds.size() >= 6); QCOMPARE(sounds.first().id, QString("duo"));
        const auto peak = [](const QByteArray& wave) { int highest = 0; for (int at = 44; at + 1 < wave.size(); at += 2) highest = std::max(highest, std::abs(int(qFromLittleEndian<qint16>(wave.constData() + at)))); return highest; };
        QSet<QByteArray> different;
        for (const auto& sound : sounds) {
            const auto wave = clipSavedSoundWave(sound.id, 50);
            QVERIFY2(wave.startsWith("RIFF") && wave.mid(8, 8) == "WAVEfmt " && wave.size() > 44 + 2000, qPrintable(sound.id));
            // Every sound peaks at the level of the earlier one at 50 %, louder above, silent at 0.
            QVERIFY2(std::abs(peak(wave) - 4202) < 60, qPrintable(sound.id + QString::number(peak(wave))));
            QVERIFY(peak(clipSavedSoundWave(sound.id, 100)) > 3 * peak(wave)); QCOMPARE(peak(clipSavedSoundWave(sound.id, 0)), 0);
            different.insert(wave); QVERIFY(!sound.name.isEmpty());
        }
        QCOMPARE(different.size(), sounds.size());
        QCOMPARE(clipSavedSoundWave("inconnu", 50), clipSavedSoundWave("duo", 50));
    }
    void settingsKeepTheClipsFolderWithTheCaptureAndLetTheSoundBeChosen() {
        QTemporaryDir directory; const auto configPath = directory.filePath("config"); QString error;
        { auto config = AppConfig::defaults(); config.libraryPath = directory.filePath("clips"); QVERIFY(ConfigStore(configPath).save(config, error)); }
        MainWindow window(configPath); window.setAttribute(Qt::WA_DontShowOnScreen); window.show(); window.setPage(MainWindow::Settings);
        auto* tabs = window.findChild<QStackedWidget*>(); QVERIFY(tabs);
        const auto inTab = [&window](const char* name, int tab) { auto* widget = window.findChild<QWidget*>(name); auto* settings = window.findChild<SettingsPage*>(); settings->showTab(tab); QCoreApplication::processEvents(); return widget && widget->isVisible(); };
        // The folder of the clips is set with the capture; the third tab is the general one.
        QVERIFY(inTab("libraryPath", 0)); QVERIFY(!inTab("libraryPath", 2)); QCOMPARE(window.findChild<QPushButton*>("settings-tab-2")->text(), QStringLiteral("Général"));
        // Each choice of a setting reads on its own line, its name in bold.
        const auto effort = window.findChild<QComboBox*>("effort")->toolTip(); QVERIFY(effort.contains("<b>Rapide</b>")); QVERIFY(effort.contains("<b>Soigné</b>"));
        QVERIFY(window.findChild<QComboBox*>("rate-control")->toolTip().contains("<b>Qualité constante</b>"));
        QVERIFY(!window.findChild<QComboBox*>("fps")->toolTip().contains("<b>"));
        QToolTip::showText(QPoint(200, 200), effort); QWidget* tip = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((tip = [] { for (auto* widget : QApplication::topLevelWidgets()) if (widget->inherits("QTipLabel") && widget->isVisible()) return widget; return static_cast<QWidget*>(nullptr); }()), 2000);
        QVERIFY(tip->width() < 600); QVERIFY(tip->grab().save(previewPath("tooltip-effort.png"))); QToolTip::hideText();
        // Quick Sync is not offered any more.
        auto* encoder = window.findChild<QComboBox*>("encoder"); QVERIFY(encoder); QCOMPARE(encoder->findData("qsv"), -1); QVERIFY(encoder->findData("nvenc") >= 0); QVERIFY(encoder->findData("amf") >= 0);
        QVERIFY(inTab("clip-saved-sound-choice", 2));
        auto* sound = window.findChild<QComboBox*>("clip-saved-sound-choice"); auto* volume = window.findChild<QSlider*>("clip-saved-sound-volume"); auto* toggle = window.findChild<QCheckBox*>("clip-saved-sound");
        QVERIFY(window.findChild<QPushButton*>("clip-saved-sound-preview")); QCOMPARE(sound->count(), int(clipSavedSounds().size())); QCOMPARE(volume->value(), 50);
        sound->setCurrentIndex(sound->findData("chime")); volume->setValue(30); QVERIFY(!window.findChild<QWidget*>("savebar")->isHidden());
        // Without a sound, its choice and its level rest.
        toggle->setChecked(false); QVERIFY(!sound->isEnabled()); toggle->setChecked(true); QVERIFY(sound->isEnabled());
        QTest::qWait(50); QVERIFY(window.grab().save(previewPath("settings-general.png")));
        window.findChild<QPushButton*>("settings-save")->click();
        AppConfig stored; QVERIFY(ConfigStore(configPath).load(stored, error)); QCOMPARE(stored.clipSavedSoundId, QString("chime")); QCOMPARE(stored.clipSavedSoundVolume, 30);
        window.close();
    }
};
QTEST_MAIN(EditorTests)
#include "editor_tests.moc"
