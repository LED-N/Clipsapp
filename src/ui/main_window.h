#pragma once
#include <QMainWindow>
#include <QFutureWatcher>
#include <QPointer>
#include <QSet>
#include "core/config_store.h"
#include "core/clip_library.h"
#include "capture/replay_engine.h"
#include "platform/global_hotkey.h"
#include "sharing_page.h"
class QCloseEvent;
class QStackedWidget;
class QSystemTrayIcon;
class QVBoxLayout;

namespace clipsapp {
class Button;
class ClipEditor;
class UpdateService;
class ClipViewer;
class DiscordBatch;
class DiscordDialog;
class LibraryPage;
class OnboardingPage;
class ReplayStrip;
class SettingsPage;
class ThumbnailCache;
class ToastHost;
class MainWindow final : public QMainWindow {
public:
    enum Page { Library, Sharing, Settings, Editor, Onboarding };
    explicit MainWindow(QString configDirectory, QWidget* parent = nullptr, ClipTrashOperation trashOperation = {});
    QJsonObject smokeReport() const;
    bool smokeCheck(QString& error);
    bool writePreviews(const QString& directory, QString& error);
    void setPage(Page page);
    // Updates: the repository followed ("owner/name"), or for tests a local description of a release.
    void followUpdates(const QString& repository, const QUrl& feed = {});
    UpdateService* updates() const { return m_updates; }
    void announceUpdated(const QString& previousVersion);
    // Set once the files of a new version are in place: the application closes and is started again.
    bool relaunchRequested() const { return m_relaunch; }
protected:
    void closeEvent(QCloseEvent* event) override;
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
    void viewClips(const QStringList& playlist, int index, bool fromEditor = false);
    void toggleCapture();
    void startCapture();
    void saveReplay();
    void captureWholeScreen();
    void updateCapture();
    void showUpdate();
    void installUpdate();
    void refreshUpdateButton();
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
    QSystemTrayIcon* m_tray;
};
}
