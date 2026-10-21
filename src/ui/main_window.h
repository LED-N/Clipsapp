#pragma once
#include <QMainWindow>
#include <QFutureWatcher>
#include <QPointer>
#include <QSet>
#include "core/auto_replay.h"
#include "core/config_store.h"
#include "core/clip_library.h"
#include "capture/replay_engine.h"
#include "platform/global_hotkey.h"
#include "sharing_page.h"
class QAction;
class QCloseEvent;
class QShowEvent;
class QStackedWidget;
class QSystemTrayIcon;
class QTimer;
class QVBoxLayout;

namespace clipsapp {
class Button;
class ClipEditor;
class FolderWatcher;
struct SourceTarget;
class UpdateService;
class ClipViewer;
class DiscordBatch;
class DiscordDialog;
class LibraryPage;
class OnboardingPage;
class ReplayStrip;
class ScreenshotEditor;
class ImageViewer;
class SettingsPage;
class ThumbnailCache;
class ToastHost;
class MainWindow final : public QMainWindow {
public:
    enum Page { Library, Sharing, Settings, Editor, Onboarding, Screenshots, ScreenshotView };
    explicit MainWindow(QString configDirectory, QWidget* parent = nullptr, ClipTrashOperation trashOperation = {});
    QJsonObject smokeReport() const;
    bool smokeCheck(QString& error);
    bool writePreviews(const QString& directory, QString& error);
    void setPage(Page page);
    // Updates: the repository followed ("owner/name"), or for tests a local description of a release.
    void followUpdates(const QString& repository, const QUrl& feed = {});
    UpdateService* updates() const { return m_updates; }
    void announceUpdated(const QString& previousVersion);
    // The journal of the versions, open on the one in use.
    void showChangelog();
    // Set once the files of a new version are in place: the application closes and is started again.
    bool relaunchRequested() const { return m_relaunch; }
    // The folder where Windows lists the user's programs: this copy keeps its shortcut there when
    // the settings ask for one. Without this call it never touches the Start menu.
    void manageStartMenu(const QString& programsDirectory);
    // Where the system lists what starts with the session: this copy keeps its entry there when the
    // settings ask for one. Without this call it never touches it.
    void manageLoginStart(const QString& location);
    // Instead of showing the window, waits behind the icon of the notification area. Refused, and
    // nothing is done, when there is no icon to come back from or the welcome page has to be seen.
    bool startInTray();
    // From now on the replay starts by itself, as the settings say. Tests never call it: nothing
    // they run captures the screen. "atLogin" leaves the session time to settle first.
    void beginAutoReplay(bool atLogin);
    // From now on the screenshot shortcut works, whether the replay runs or not. Tests never call it:
    // a shortcut is the whole system's, and the copy of Clipsapp in use may hold it.
    void armShortcuts();
    // A screenshot of the captured screen, as the shortcut takes it.
    void takeScreenshot();
    // What the recording shortcut does: starts a recording, the replay with it if it is stopped, or stops it.
    void toggleRecording();
    // A crash report left by the previous launch.
    void announceCrash(const QString& report);
    // Brings the window back from the notification area, as when the application is started again.
    void surface();
    // The window of the application: closing it for good, from its icon or from an update, ends the program.
    void quitWhenClosed();
    // As "Quitter" in the menu of the icon: settings left unsaved are asked about first.
    void quit();
protected:
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;
private:
    QWidget* buildRail();
    void rebuildRail();
    void updateNav();
    bool dirty() const;
    bool saveConfig(bool announce = true);
    void revertConfig();
    void refreshLibrary();
    void rebuildLibrary();
    void refreshSharing();
    void updateBusy();
    void startDiscordSharing(QStringList paths, bool fromEditor = false);
    void savedFeedback(const QString& path);
    void classifyClips(const QStringList& paths, const QString& game);
    void deleteClips(QStringList paths, bool confirmed = false);
    void renameClip(const QString& path, const QString& name);
    void copyFiles(const QStringList& paths);
    void revealFile(const QString& path);
    void chooseLibraryFolder();
    void openClip(const QString& path);
    void openScreenshot(const QString& path);
    void viewImages(const QStringList& playlist, int index);
    void refreshScreenshots();
    void rebuildScreenshots();
    void screenshotFromClip(const QString& path, qint64 positionMs, const QRect& crop);
    void screenshotDone();
    void bindScreenshotHotkey(bool announce);
    void bindRecordHotkey(bool announce);
    // The logo in the colors of the theme, on the window and its icon; with "files", the Start menu's too.
    void refreshIcons(bool files);
    // The game a clip or a screenshot belongs to: its classification, or its folder. Empty when it has none.
    QString gameOf(const QString& path) const;
    TrackMix mixForClip(const QString& path) const;
    void adoptGameMix(const QString& game, const TrackMix& mix);
    void viewClips(const QStringList& playlist, int index, bool fromEditor = false);
    void toggleCapture();
    void startCapture(ReplayEngine::Purpose purpose = ReplayEngine::Purpose::Replay);
    // The replay the user stops: what starts it by itself waits until its reason comes again.
    void stopCapture();
    bool startAutomatically(const QString& reason);
    void autoReplayTick();
    const SourceTarget* mainScreen() const;
    void copyDiagnostic();
    void saveReplay();
    void captureWholeScreen();
    void updateCapture();
    void showUpdate();
    void installUpdate();
    void refreshUpdateButton();
    void refreshTray();
    // The usual mix: handed to what plays and exports clips, replaced on request, learnt from the exports.
    void spreadMix();
    void adoptMix(const TrackMix& mix);
    void learnMix(const TrackMix& levels, const QString& game);
    QString mixText(const TrackMix& from, const TrackMix& to) const;
    QString targetName(const VideoSettings& video) const;
    RecordingProfile& profile();

    ConfigStore m_store;
    LibraryLabels m_labels;
    AppConfig m_config, m_saved;
    QString m_loadError, m_replayError;
    bool m_rescanPending = false, m_shareRescanPending = false, m_restartPending = false, m_scanned = false;
    ReplayEngine* m_replay;
    GlobalHotkey* m_globalHotkey;
    QList<ClipEntry> m_clips;
    QList<ShareCopy> m_copies;
    QSet<QString> m_fresh;
    QStringList m_selectAfterScan;
    QString m_reopenAfterRename;
    QFutureWatcher<LibraryScan>* m_scan;
    QFutureWatcher<ClipTrashResult>* m_trash;
    QFutureWatcher<LibraryScan>* m_shareScan;
    QFutureWatcher<ClipRename>* m_rename;
    QString m_renaming;
    ClipTrashOperation m_trashOperation;
    DiscordBatch* m_batch;
    QPointer<DiscordDialog> m_shareDialog;
    ThumbnailCache* m_cache;
    ToastHost* m_toasts;
    ReplayStrip* m_strip;
    QWidget* m_rail;
    QWidget* m_clipsNav; QWidget* m_shareNav; QWidget* m_settingsNav; QVBoxLayout* m_subNav; QWidget* m_subNavHost;
    QStackedWidget* m_pages;
    LibraryPage* m_library;
    SharingPage* m_sharing;
    SettingsPage* m_settings;
    ClipEditor* m_editor;
    ClipViewer* m_viewer = nullptr;
    OnboardingPage* m_onboarding;
    int m_libraryFilter = 0; QString m_libraryGame;
    bool m_firstLaunch = false, m_relaunch = false;
    UpdateService* m_updates; Button* m_updateButton;
    QSystemTrayIcon* m_tray; QAction* m_traySave; QAction* m_trayToggle; QAction* m_trayRecord;
    QString m_programs; bool m_quitting = false, m_quitOnClose = false; TrackMix m_mixAsked;
    QString m_configDirectory, m_loginLocation, m_scanWarning, m_themeIcon; bool m_trayStart = false, m_sharesScanned = false;
    AutoReplay m_auto; QTimer* m_autoWatch; bool m_autoArmed = false;
    // The folder of the clips, followed for what the Explorer adds, renames or removes in it.
    FolderWatcher* m_folderWatch; QTimer* m_folderSettle; bool m_folderLibrary = false, m_folderSharing = false, m_folderShots = false;
    // Screenshots: their page, their editor, what the folder holds, the shortcut and the capture under way.
    LibraryPage* m_shots; ScreenshotEditor* m_shotEditor; ImageViewer* m_imageViewer = nullptr; QList<ClipEntry> m_images; QSet<QString> m_freshShots;
    QFutureWatcher<LibraryScan>* m_imageScan; bool m_imageRescanPending = false, m_imagesScanned = false;
    GlobalHotkey* m_shotHotkey; GlobalHotkey* m_recordHotkey; bool m_shortcutsArmed = false; QFutureWatcher<QString>* m_shotTask;
    QWidget* m_shotsNav; QVBoxLayout* m_shotSubNav; QWidget* m_shotSubNavHost; int m_shotFilter = 0; QString m_shotGame, m_mixAskedGame, m_shotLabel;
};
}
