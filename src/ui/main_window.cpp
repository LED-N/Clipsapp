#include "main_window.h"
#include "clip_editor.h"
#include "clip_feedback.h"
#include "clip_list.h"
#include "clip_viewer.h"
#include "discord_batch.h"
#include "discord_dialog.h"
#include "library_page.h"
#include "onboarding_page.h"
#include "replay_strip.h"
#include "settings_page.h"
#include "thumbnail_cache.h"
#include "update_service.h"
#include "widgets.h"
#include "platform/start_menu.h"
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QProcess>
#include <QRegularExpression>
#include <QScrollArea>
#include <QShortcut>
#include <QStackedWidget>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <utility>

namespace clipsapp {
namespace {
const QString unclassified = QStringLiteral("Non classé");
class NavButton final : public QPushButton {
public:
    NavButton(const QString& iconName, const QString& text, bool sub = false) : QPushButton(text), m_icon(iconName), m_sub(sub) { setCheckable(true); setAttribute(Qt::WA_Hover); setCursor(Qt::PointingHandCursor); setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed); }
    void setNote(const QString& note, bool dot = false) { if (note == m_note && dot == m_dot) return; m_note = note; m_dot = dot; update(); }
    QSize sizeHint() const override { return {120, m_sub ? 30 : 36}; }
    QSize minimumSizeHint() const override { return {60, m_sub ? 30 : 36}; }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing); const bool on = isChecked(), over = underMouse() && isEnabled();
        if (on || over) { painter.setPen(Qt::NoPen); painter.setBrush(on && !m_sub ? QColor(0x1C, 0x22, 0x2B) : QColor(0x17, 0x1B, 0x22)); painter.drawRoundedRect(rect(), 6, 6); }
        const QColor color = !isEnabled() ? tk::textOff : on || over ? tk::text : tk::text2;
        if (!m_sub) paintIcon(painter, m_icon, QRectF(10, (height() - 16) / 2.0, 16, 16), color);
        const auto noteFont = uiFont(12, QFont::DemiBold); const int noteWidth = m_note.isEmpty() ? 0 : QFontMetrics(noteFont).horizontalAdvance(m_note) + (m_dot ? 12 : 0);
        const auto font = uiFont(13, m_sub ? QFont::Normal : QFont::DemiBold); painter.setFont(font); painter.setPen(color);
        painter.drawText(QRectF(36, 0, width() - 36 - 10 - noteWidth - (noteWidth ? 8 : 0), height()), Qt::AlignVCenter | Qt::AlignLeft, QFontMetrics(font).elidedText(text(), Qt::ElideRight, width() - 46 - noteWidth - (noteWidth ? 8 : 0)));
        if (m_note.isEmpty()) return;
        painter.setFont(noteFont); painter.setPen(isEnabled() ? tk::text3 : tk::textOff); painter.drawText(QRectF(0, 0, width() - 10, height()), Qt::AlignVCenter | Qt::AlignRight, m_note);
        if (m_dot) { painter.setPen(Qt::NoPen); painter.setBrush(tk::info); painter.drawEllipse(QRectF(width() - 10 - noteWidth, height() / 2.0 - 3, 6, 6)); }
    }
private:
    QString m_icon, m_note; bool m_sub, m_dot = false;
};
NavButton* nav(QWidget* widget) { return static_cast<NavButton*>(widget); }
// A confirmation in the application's own clothes. The safe answer has the focus.
int choose(QWidget* parent, const char* name, const QString& title, const QString& text, const QStringList& buttons, int primary, int safe) {
    ModalDialog dialog(parent, 440); dialog.setObjectName(name); dialog.setHeading(title, text, nullptr, false); dialog.body()->setContentsMargins(22, 4, 22, 12);
    int chosen = -1;
    for (int index = 0; index < buttons.size(); ++index) {
        auto* button = new Button(buttons[index], index == primary ? Button::Primary : Button::Secondary); button->setObjectName(QStringLiteral("choice-%1").arg(index)); dialog.footer()->addWidget(button);
        QObject::connect(button, &QPushButton::clicked, &dialog, [&dialog, &chosen, index] { chosen = index; dialog.accept(); });
        if (index == safe) { button->setDefault(true); button->setFocus(); }
    }
    dialog.exec(); return chosen;
}
bool confirmTrash(QWidget* parent, const QStringList& paths, bool copies) {
    ModalDialog dialog(parent, 480); dialog.setObjectName("delete-clips-prompt");
    QStringList names; for (const auto& path : paths) names.append(QFileInfo(path).fileName()); dialog.setProperty("names", names);
    const auto count = paths.size(); const QString what = copies ? (count > 1 ? QStringLiteral("%1 copies").arg(count) : QStringLiteral("cette copie")) : (count > 1 ? QStringLiteral("%1 clips").arg(count) : QStringLiteral("ce clip"));
    dialog.setHeading(QStringLiteral("Mettre %1 à la corbeille ?").arg(what), count > 1 ? QStringLiteral("Vous pourrez les récupérer depuis la corbeille du système.") : QStringLiteral("Vous pourrez le récupérer depuis la corbeille du système."),
        new IconBox(QStringLiteral("trash"), tk::danger, tk::dangerSoft), false);
    dialog.body()->setSpacing(10);
    auto* well = card("well"); auto* wellLayout = new QVBoxLayout(well); wellLayout->setContentsMargins(1, 6, 1, 6);
    auto* rows = new QWidget; auto* rowsLayout = new QVBoxLayout(rows); rowsLayout->setContentsMargins(0, 0, 0, 0); rowsLayout->setSpacing(0);
    for (const auto& path : paths) {
        const QFileInfo file(path); auto* line = new QHBoxLayout; line->setContentsMargins(12, 5, 12, 5); line->setSpacing(12);
        line->addWidget(new ElideLabel(file.completeBaseName(), nullptr, Qt::ElideMiddle), 1); line->addWidget(monoLabel(formatBytes(file.size()), 12, tk::text2)); rowsLayout->addLayout(line);
    }
    auto* scroll = new QScrollArea; scroll->setWidgetResizable(true); scroll->setWidget(rows); scroll->setFrameShape(QFrame::NoFrame); scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFixedHeight(int(std::min<qsizetype>(6, count) * 30)); wellLayout->addWidget(scroll); dialog.body()->addWidget(well);
    if (!copies) { auto* hint = new QHBoxLayout; hint->setSpacing(6); hint->addWidget(new IconLabel(QStringLiteral("info"), tk::text2, 14)); hint->addWidget(label(QStringLiteral("Les copies Discord déjà préparées sont conservées."), "hint"), 1); dialog.body()->addLayout(hint); }
    auto* keep = new Button(copies ? (count > 1 ? QStringLiteral("Garder les copies") : QStringLiteral("Garder la copie")) : (count > 1 ? QStringLiteral("Garder les clips") : QStringLiteral("Garder le clip"))); keep->setObjectName("trash-keep");
    auto* confirm = new Button(QStringLiteral("trash"), QStringLiteral("Mettre à la corbeille"), Button::DangerSolid); confirm->setObjectName("trash-confirm"); confirm->setAutoDefault(false);
    dialog.footer()->addWidget(keep); dialog.footer()->addWidget(confirm); keep->setDefault(true); keep->setFocus();
    QObject::connect(keep, &QPushButton::clicked, &dialog, &QDialog::reject); QObject::connect(confirm, &QPushButton::clicked, &dialog, &QDialog::accept);
    return dialog.exec() == QDialog::Accepted;
}
QString firstLine(const QString& text) { return text.section('\n', 0, 0).trimmed(); }
}
MainWindow::MainWindow(QString configDirectory, QWidget* parent, ClipTrashOperation trashOperation)
    : QMainWindow(parent), m_store(configDirectory), m_labels(QDir(configDirectory).filePath("library-tags.json")), m_config(AppConfig::defaults()), m_scan(new QFutureWatcher<LibraryScan>(this)), m_trash(new QFutureWatcher<ClipTrashResult>(this)), m_shareScan(new QFutureWatcher<LibraryScan>(this)), m_rename(new QFutureWatcher<ClipRename>(this)), m_trashOperation(std::move(trashOperation)) {
    m_firstLaunch = !QFile::exists(m_store.filePath());
    m_store.load(m_config, m_loadError); m_saved = m_config;
    QString labelsError; m_labels.load(labelsError);
    m_replay = new ReplayEngine(this); m_globalHotkey = new GlobalHotkey(this); m_batch = new DiscordBatch(this); m_cache = new ThumbnailCache(this);
    m_updates = new UpdateService(QStringLiteral(CLIPSAPP_VERSION), QCoreApplication::applicationDirPath(), this);
    setWindowTitle(QStringLiteral("Clipsapp[*]")); resize(1220, 820); setMinimumSize(980, 650); setWindowIcon(appIcon());
    m_tray = new QSystemTrayIcon(windowIcon(), this); m_tray->setToolTip("Clipsapp");
    // A click brings the window back; the menu of the right button acts on the replay without it.
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) { if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) surface(); });
    connect(m_tray, &QSystemTrayIcon::messageClicked, this, [this] { surface(); });
    auto* trayMenu = new QMenu(this); trayMenu->setObjectName("tray-menu");
    connect(trayMenu->addAction(QStringLiteral("Ouvrir Clipsapp")), &QAction::triggered, this, [this] { surface(); });
    trayMenu->addSeparator();
    m_traySave = trayMenu->addAction(QStringLiteral("Sauvegarder le clip")); connect(m_traySave, &QAction::triggered, this, [this] { saveReplay(); });
    m_trayToggle = trayMenu->addAction(QString()); connect(m_trayToggle, &QAction::triggered, this, [this] { toggleCapture(); });
    trayMenu->addSeparator();
    connect(trayMenu->addAction(QStringLiteral("Quitter Clipsapp")), &QAction::triggered, this, [this] { quit(); });
    m_tray->setContextMenu(trayMenu);

    auto* root = new QWidget; setCentralWidget(root); auto* vertical = new QVBoxLayout(root); vertical->setContentsMargins(0, 0, 0, 0); vertical->setSpacing(0);
    m_strip = new ReplayStrip; vertical->addWidget(m_strip);
    auto* horizontal = new QHBoxLayout; horizontal->setSpacing(0); horizontal->addWidget(buildRail());
    m_pages = new QStackedWidget; m_library = new LibraryPage(m_cache); m_sharing = new SharingPage(m_cache, m_batch); m_settings = new SettingsPage(m_config, m_store.filePath()); m_editor = new ClipEditor; m_onboarding = new OnboardingPage(m_config);
    for (auto* page : std::initializer_list<QWidget*>{m_library, m_sharing, m_settings, m_editor, m_onboarding}) m_pages->addWidget(page);
    horizontal->addWidget(m_pages, 1); vertical->addLayout(horizontal, 1);
    m_toasts = new ToastHost(m_pages);

    // Replay strip
    connect(m_strip, &ReplayStrip::startRequested, this, [this] { startCapture(); });
    connect(m_strip, &ReplayStrip::stopRequested, this, [this] { m_restartPending = false; m_replay->stop(); });
    connect(m_strip, &ReplayStrip::saveRequested, this, [this] { saveReplay(); });
    connect(m_strip, &ReplayStrip::fullScreenRequested, this, [this] { captureWholeScreen(); });
    connect(m_strip, &ReplayStrip::settingsRequested, this, [this] { m_settings->showTab(0); setPage(Settings); });
    connect(m_globalHotkey, &GlobalHotkey::activated, this, [this] { saveReplay(); });
    connect(m_replay, &ReplayEngine::changed, this, [this] { updateCapture(); });
    connect(m_replay, &ReplayEngine::failed, this, [this](const QString& error) {
        // A capture that stops stays written in the strip until the user acts; a failed save is a notification.
        if (m_replay->state() == ReplayEngine::State::Error) { m_replayError = firstLine(error); m_restartPending = false; updateCapture(); }
        else m_toasts->show(ToastHost::Error, QStringLiteral("Le clip n'a pas pu être sauvegardé"), error);
    });
    connect(m_replay, &ReplayEngine::saved, this, [this](const QString& path, double seconds) {
        m_fresh.insert(path); savedFeedback(path); refreshLibrary();
        m_toasts->show(ToastHost::Ok, QStringLiteral("Clip sauvegardé"), QStringLiteral("%1 · %2 s · %3").arg(QFileInfo(path).completeBaseName()).arg(qRound(seconds)).arg(formatBytes(QFileInfo(path).size())),
            {{QStringLiteral("Ouvrir dans l'éditeur"), [this, path] { openClip(path); }}, {QStringLiteral("Préparer pour Discord"), [this, path] { startDiscordSharing({path}); }}});
    });

    // Library
    connect(m_library, &LibraryPage::openRequested, this, &MainWindow::openClip);
    connect(m_library, &LibraryPage::viewRequested, this, [this](const QStringList& playlist, int index) { viewClips(playlist, index); });
    connect(m_sharing, &SharingPage::viewRequested, this, [this](const QStringList& playlist, int index) { viewClips(playlist, index); });
    connect(m_editor, &ClipEditor::fullScreenRequested, this, [this] { viewClips({m_editor->clipPath()}, 0, true); });
    connect(m_library, &LibraryPage::shareRequested, this, [this](const QStringList& paths) { startDiscordSharing(paths); });
    connect(m_library, &LibraryPage::trashRequested, this, [this](const QStringList& paths) { deleteClips(paths); });
    connect(m_library, &LibraryPage::classifyRequested, this, &MainWindow::classifyClips);
    connect(m_library, &LibraryPage::renameRequested, this, &MainWindow::renameClip);
    connect(m_library, &LibraryPage::copyRequested, this, &MainWindow::copyFiles);
    connect(m_library, &LibraryPage::revealRequested, this, &MainWindow::revealFile);
    connect(m_library, &LibraryPage::startReplayRequested, this, [this] { startCapture(); });
    connect(m_library, &LibraryPage::chooseFolderRequested, this, [this] { chooseLibraryFolder(); });
    connect(m_library, &LibraryPage::favoriteRequested, this, [this](const QStringList& paths, bool favorite) {
        QString error; if (!m_labels.setFavorites(paths, favorite, error)) m_toasts->show(ToastHost::Error, QStringLiteral("Favoris non enregistrés"), error);
        rebuildLibrary();
    });
    for (auto* list : {m_library->list(), m_sharing->list()}) {
        connect(list, &ClipListView::fileDragStarted, this, [this](int count, int unavailable) {
            m_toasts->show(ToastHost::Info, count > 1 ? QStringLiteral("Déposez les %1 fichiers dans Discord").arg(count) : QStringLiteral("Déposez le fichier dans Discord"),
                QStringLiteral("Dans un message privé ou un salon, puis validez l'envoi.") + (unavailable ? QStringLiteral(" %1 fichier(s) indisponible(s) écarté(s).").arg(unavailable) : QString()));
        });
        connect(list, &ClipListView::fileDragUnavailable, this, [this] { m_toasts->show(ToastHost::Warn, QStringLiteral("Fichiers introuvables"), QStringLiteral("La sélection n'existe plus sur le disque. La liste est actualisée.")); refreshLibrary(); refreshSharing(); });
    }
    connect(m_library, &LibraryPage::selectionBarVisible, this, [this](bool visible) { if (m_pages->currentWidget() == m_library) m_toasts->setBottomInset(visible ? 88 : 20); });
    connect(m_sharing, &SharingPage::selectionBarVisible, this, [this](bool visible) { if (m_pages->currentWidget() == m_sharing) m_toasts->setBottomInset(visible ? 88 : 20); });

    // Sharing
    connect(m_sharing, &SharingPage::copyRequested, this, &MainWindow::copyFiles);
    connect(m_sharing, &SharingPage::revealRequested, this, &MainWindow::revealFile);
    connect(m_sharing, &SharingPage::trashRequested, this, [this](const QStringList& paths) { deleteClips(paths); });
    connect(m_sharing, &SharingPage::openOriginalRequested, this, &MainWindow::openClip);
    connect(m_sharing, &SharingPage::openFolderRequested, this, [this] { const auto directory = QDir(m_config.libraryPath).filePath("Partage Discord"); QDir().mkpath(directory); QDesktopServices::openUrl(QUrl::fromLocalFile(directory)); });
    connect(m_sharing, &SharingPage::limitChosen, this, [this](qint64 bytes) {
        m_config.discordLimitBytes = m_saved.discordLimitBytes = bytes; QString error; if (!m_store.save(m_saved, error)) m_toasts->show(ToastHost::Error, QStringLiteral("Limite non enregistrée"), error);
        m_settings->setSaved(m_saved); rebuildLibrary(); m_sharing->setCopies(m_copies, bytes);
    });
    connect(m_batch, &DiscordBatch::runningChanged, this, [this] { updateBusy(); updateNav(); });
    connect(m_batch, &DiscordBatch::changed, this, [this] { updateNav(); });
    connect(m_batch, &DiscordBatch::shareReady, this, [this](const QString& path, const QString& source, qint64 limit) {
        QString error; m_labels.setShareLink(path, source, limit, error); refreshSharing();
        if (!m_shareDialog) m_toasts->show(ToastHost::Info, QStringLiteral("Copie Discord prête · %1").arg(formatBytes(QFileInfo(path).size())), QStringLiteral("Glissez-la depuis Partage Discord."), {{QStringLiteral("Voir"), [this] { setPage(Sharing); }}});
    });
    connect(m_batch, &DiscordBatch::highQualityReady, this, [this](const QString& path, const QString& original, const ClipEdit& edit) {
        const auto game = m_labels.gameFor(original, {}); QString error; if (!game.isEmpty()) m_labels.setGame(path, game, error);
        m_editor->noteHighQualityExport(original, edit); refreshLibrary();
        if (m_editor->clipPath() == original && !m_editor->clipInfo().mixed) learnMix(editLevels(edit));
    });
    connect(m_batch, &DiscordBatch::finished, this, [this](int, int failed, bool cancelled) {
        refreshSharing(); if (m_shareDialog) return;
        if (failed) m_toasts->show(ToastHost::Warn, failed > 1 ? QStringLiteral("%1 copies Discord ont échoué").arg(failed) : QStringLiteral("Une copie Discord a échoué"), QStringLiteral("Elles attendent dans Partage Discord, sous « À reprendre »."), {{QStringLiteral("Voir"), [this] { setPage(Sharing); }}});
        else if (cancelled) m_toasts->show(ToastHost::Info, QStringLiteral("Lot annulé"), QStringLiteral("Les copies déjà prêtes sont conservées."));
    });

    // Settings
    connect(m_settings, &SettingsPage::edited, this, [this] { setWindowModified(dirty()); updateCapture(); });
    connect(m_settings, &SettingsPage::saveRequested, this, [this] { saveConfig(); });
    connect(m_settings, &SettingsPage::revertRequested, this, [this] { revertConfig(); });
    connect(m_settings, &SettingsPage::stopReplayRequested, this, [this] { m_restartPending = false; m_replay->stop(); });
    connect(m_settings, &SettingsPage::chooseFolderRequested, this, [this] { chooseLibraryFolder(); });
    connect(m_settings, &SettingsPage::openSettingsFolderRequested, this, [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_store.filePath()).absolutePath())); });
    connect(m_settings, &SettingsPage::notice, this, [this](const QString& title, const QString& text) { m_toasts->show(ToastHost::Ok, title, text); });

    // Updates
    connect(m_settings, &SettingsPage::checkUpdatesRequested, this, [this] { m_updates->check(true); });
    connect(m_updates, &UpdateService::changed, this, [this] { refreshUpdateButton(); });
    connect(m_updates, &UpdateService::available, this, [this](bool manual) {
        if (manual) { showUpdate(); return; }
        m_toasts->show(ToastHost::Info, QStringLiteral("Clipsapp %1 est disponible").arg(m_updates->info().version.toString()), QStringLiteral("Vous utilisez la version %1.").arg(m_updates->currentVersion()),
            {{QStringLiteral("Voir la mise à jour"), [this] { showUpdate(); }}}, QStringLiteral("save"));
    });
    connect(m_updates, &UpdateService::upToDate, this, [this](bool manual) { if (manual) m_toasts->show(ToastHost::Ok, QStringLiteral("Clipsapp est à jour"), QStringLiteral("La version %1 est la dernière publiée.").arg(m_updates->currentVersion())); });
    connect(m_updates, &UpdateService::checkFailed, this, [this](const QString& error, bool manual) { if (manual) m_toasts->show(ToastHost::Warn, QStringLiteral("Vérification impossible"), error); });
    // The application often stays open for days beside the games: it looks again four times a day.
    auto* again = new QTimer(this); again->setInterval(6 * 60 * 60 * 1000); again->start();
    connect(again, &QTimer::timeout, this, [this] { if (m_saved.checkUpdates) m_updates->check(); });

    // Editor
    connect(m_editor, &ClipEditor::backRequested, this, [this] { setPage(Library); });
    connect(m_editor, &ClipEditor::discordRequested, this, [this](const ClipInfo&, const ClipEdit&) { startDiscordSharing({m_editor->clipPath()}, true); });
    connect(m_editor, &ClipEditor::exported, this, [this](const QString& path, const QString& original) {
        const auto game = m_labels.gameFor(original, {}); QString error;
        if (!game.isEmpty() && !m_labels.setGame(path, game, error)) m_toasts->show(ToastHost::Warn, QStringLiteral("Classement non repris"), error);
        m_fresh.insert(path); refreshLibrary();
        if (m_editor->clipPath() == original && !m_editor->clipInfo().mixed) learnMix(editLevels(m_editor->exportedEdit()));
    });
    connect(m_editor, &ClipEditor::mixChosen, this, [this](const TrackMix& mix) {
        adoptMix(mix); m_toasts->show(ToastHost::Ok, QStringLiteral("Volumes habituels enregistrés"), QStringLiteral("Les prochains clips s'ouvrent avec ces volumes. Ils se règlent aussi dans Réglages › Audio."));
    });
    connect(m_editor, &ClipEditor::deleteRequested, this, [this](const QString& path, bool confirmed) { deleteClips({path}, confirmed); });
    connect(m_editor, &ClipEditor::renameRequested, this, &MainWindow::renameClip);
    connect(m_editor, &ClipEditor::openRequested, this, &MainWindow::openClip);
    connect(m_editor, &ClipEditor::failed, this, [this](const QString& title, const QString& detail) { m_toasts->show(ToastHost::Error, title, detail); });

    // Onboarding
    // The welcome page writes its choices straight into the configuration; the settings form follows before it is read again.
    connect(m_onboarding, &OnboardingPage::chooseFolderRequested, this, [this] { m_settings->load(); chooseLibraryFolder(); m_onboarding->refresh(); });
    connect(m_onboarding, &OnboardingPage::finished, this, [this](bool start) {
        m_config.startMenuShortcut = m_onboarding->startMenuWanted();
        m_firstLaunch = false; m_settings->load(); saveConfig(false); setPage(Library);
        if (start) startCapture();
    });

    connect(m_trash, &QFutureWatcher<ClipTrashResult>::finished, this, [this] {
        const auto result = m_trash->result(); QString error;
        if (!result.removed.isEmpty()) {
            m_labels.forget(result.removed, error);
            for (const auto& path : result.removed) { m_editor->forgetClip(path); m_fresh.remove(path); }
        }
        m_cache->setActive(true); updateBusy(); refreshLibrary(); refreshSharing();
        const auto count = result.removed.size();
        if (!result.errors.isEmpty()) m_toasts->show(ToastHost::Warn, QStringLiteral("%1 fichier(s) sont restés en place").arg(result.errors.size()), result.errors.first());
        else if (count) m_toasts->show(ToastHost::Ok, count > 1 ? QStringLiteral("%1 fichiers mis à la corbeille").arg(count) : QStringLiteral("Fichier mis à la corbeille"), QStringLiteral("La corbeille du système permet de les récupérer."));
        if (!error.isEmpty()) m_toasts->show(ToastHost::Warn, QStringLiteral("Classement non mis à jour"), error);
    });
    connect(m_rename, &QFutureWatcher<ClipRename>::finished, this, [this] {
        const auto result = m_rename->result(); const auto from = m_renaming; m_renaming.clear(); m_cache->setActive(true); updateBusy();
        const auto reopen = m_reopenAfterRename; m_reopenAfterRename.clear();
        if (!result.error.isEmpty()) { m_toasts->show(ToastHost::Error, QStringLiteral("Le clip n'a pas été renommé"), result.error); if (!reopen.isEmpty()) openClip(from); return; }
        QString error; m_labels.moveClip(from, result.path, error); m_editor->moveClip(from, result.path);
        if (m_fresh.remove(from)) m_fresh.insert(result.path);
        m_selectAfterScan = {result.path}; refreshLibrary(); refreshSharing();
        if (!reopen.isEmpty()) openClip(result.path);
    });
    connect(m_shareScan, &QFutureWatcher<LibraryScan>::finished, this, [this] {
        if (m_shareRescanPending) { m_shareRescanPending = false; refreshSharing(); return; }
        const auto scan = m_shareScan->result(); m_copies.clear();
        for (const auto& clip : scan.clips) m_copies.append({clip, m_labels.shareLink(clip.path)});
        m_sharing->setCopies(m_copies, m_config.discordLimitBytes); updateNav(); rebuildLibrary();
    });
    connect(m_scan, &QFutureWatcher<LibraryScan>::finished, this, [this] {
        const auto result = m_scan->result();
        if (m_rescanPending) { m_rescanPending = false; refreshLibrary(); return; }
        m_clips = result.clips; m_scanned = true;
        for (auto& clip : m_clips) clip.game = m_labels.gameFor(clip.path, clip.game);
        m_library->setLoading(false); rebuildLibrary();
        if (!m_selectAfterScan.isEmpty()) { m_library->select(m_selectAfterScan); m_selectAfterScan.clear(); }
        if (!result.warning.isEmpty()) m_toasts->show(ToastHost::Warn, QStringLiteral("Bibliothèque volumineuse"), result.warning);
    });
    auto* find = new QShortcut(QKeySequence::Find, this); connect(find, &QShortcut::activated, this, [this] { if (m_pages->currentWidget() == m_library) m_library->focusSearch(); });

    m_settings->refreshCatalog(); m_settings->load(); m_settings->setSaved(m_saved); m_onboarding->setCatalog(m_settings->catalog()); spreadMix();
    setPage(m_firstLaunch ? Onboarding : Library);
    if (!m_loadError.isEmpty()) m_toasts->show(ToastHost::Warn, QStringLiteral("Réglages illisibles"), QStringLiteral("Les réglages par défaut sont affichés. Le fichier d'origine sera conservé à côté du nouveau."));
    if (!labelsError.isEmpty()) m_toasts->show(ToastHost::Warn, QStringLiteral("Classement illisible"), labelsError);
    m_library->setLoading(true); refreshLibrary(); refreshSharing(); updateCapture();
}
QWidget* MainWindow::buildRail() {
    auto* rail = new QFrame; m_rail = rail; rail->setObjectName("sidebar"); rail->setFixedWidth(208); rail->setStyleSheet(QStringLiteral("QFrame#sidebar { background: #101318; border: none; border-right: 1px solid #242A34; }"));
    auto* layout = new QVBoxLayout(rail); layout->setContentsMargins(10, 12, 11, 14); layout->setSpacing(2);
    auto* clips = new NavButton(QStringLiteral("clips"), QStringLiteral("Clips")); clips->setObjectName("nav-clips"); m_clipsNav = clips; layout->addWidget(clips);
    m_subNavHost = new QWidget; m_subNav = new QVBoxLayout(m_subNavHost); m_subNav->setContentsMargins(0, 0, 0, 0); m_subNav->setSpacing(1);
    auto* scroll = new QScrollArea; scroll->setObjectName("nav-games"); scroll->setWidgetResizable(true); scroll->setWidget(m_subNavHost); scroll->setFrameShape(QFrame::NoFrame); scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); scroll->setMaximumHeight(0);
    // The list of games takes the height it needs before the empty space below does, and scrolls only when the rail is full.
    layout->addWidget(scroll, 1000); layout->addSpacing(8);
    auto* share = new NavButton(QStringLiteral("share"), QStringLiteral("Partage Discord")); share->setObjectName("nav-sharing"); m_shareNav = share; layout->addWidget(share);
    auto* settings = new NavButton(QStringLiteral("settings"), QStringLiteral("Réglages")); settings->setObjectName("nav-settings"); m_settingsNav = settings; layout->addWidget(settings); layout->addStretch(1);
    // A newer version waits here, out of the way, until the user wants it.
    m_updateButton = new Button(QStringLiteral("save"), QString(), Button::Primary, Button::Small); m_updateButton->setObjectName("update-available"); m_updateButton->hide(); layout->addWidget(m_updateButton); layout->addSpacing(8);
    connect(m_updateButton, &QPushButton::clicked, this, [this] { showUpdate(); });
    auto* foot = new QVBoxLayout; foot->setContentsMargins(10, 0, 10, 0); foot->setSpacing(2); auto* local = new QHBoxLayout; local->setSpacing(6);
    local->addWidget(new IconLabel(QStringLiteral("lock"), tk::text2, 14)); local->addWidget(label(QStringLiteral("Vos clips restent sur ce PC"), "small"), 1); foot->addLayout(local);
    foot->addWidget(label(QStringLiteral("Version %1").arg(CLIPSAPP_VERSION), "small3")); layout->addLayout(foot);
    connect(clips, &QPushButton::clicked, this, [this] { m_libraryFilter = LibraryPage::All; m_libraryGame.clear(); m_library->setFilter(LibraryPage::All); setPage(Library); });
    connect(share, &QPushButton::clicked, this, [this] { setPage(Sharing); refreshSharing(); });
    connect(settings, &QPushButton::clicked, this, [this] { setPage(Settings); });
    return rail;
}
void MainWindow::rebuildRail() {
    while (auto* child = m_subNav->takeAt(0)) { if (child->widget()) { child->widget()->hide(); child->widget()->deleteLater(); } delete child; }
    QHash<QString, int> counts; int favorites = 0; for (const auto& clip : m_clips) { ++counts[clip.game]; if (m_labels.isFavorite(clip.path)) ++favorites; }
    QList<QPair<QString, int>> games; for (auto it = counts.cbegin(); it != counts.cend(); ++it) if (it.key() != unclassified) games.append({it.key(), it.value()});
    std::sort(games.begin(), games.end(), [](const auto& left, const auto& right) { return left.second != right.second ? left.second > right.second : left.first.localeAwareCompare(right.first) < 0; });
    int rows = 0;
    const auto add = [&](const QString& text, int count, int filter, const QString& game) {
        auto* button = new NavButton(QString(), text, true); button->setNote(QString::number(count)); button->setProperty("filter", filter); button->setProperty("game", game); button->setToolTip(text); m_subNav->addWidget(button); ++rows;
        connect(button, &QPushButton::clicked, this, [this, filter, game] { m_libraryFilter = filter; m_libraryGame = game; m_library->setFilter(LibraryPage::Filter(filter), game); setPage(Library); });
    };
    if (!m_clips.isEmpty()) add(QStringLiteral("Favoris"), favorites, LibraryPage::Favorites, {});
    for (const auto& game : games) add(game.first, game.second, LibraryPage::Game, game.first);
    if (counts.contains(unclassified) && !games.isEmpty()) add(QStringLiteral("Non classés"), counts.value(unclassified), LibraryPage::Game, unclassified);
    // A filter whose last clip has gone falls back to every clip.
    if (m_libraryFilter == LibraryPage::Game && !counts.contains(m_libraryGame)) { m_libraryFilter = LibraryPage::All; m_libraryGame.clear(); m_library->setFilter(LibraryPage::All); }
    m_rail->findChild<QScrollArea*>("nav-games")->setMaximumHeight(rows ? rows * 31 + 3 : 0);
    nav(m_clipsNav)->setNote(QString::number(m_clips.size())); updateNav();
}
void MainWindow::updateNav() {
    const auto* current = m_pages->currentWidget(); const bool library = current == m_library;
    nav(m_clipsNav)->setChecked(library && m_libraryFilter == LibraryPage::All); nav(m_shareNav)->setChecked(current == m_sharing); nav(m_settingsNav)->setChecked(current == m_settings);
    for (int index = 0; index < m_subNav->count(); ++index) if (auto* button = qobject_cast<QPushButton*>(m_subNav->itemAt(index)->widget()))
        button->setChecked(library && button->property("filter").toInt() == m_libraryFilter && button->property("game").toString() == m_libraryGame);
    if (m_batch->running()) nav(m_shareNav)->setNote(QStringLiteral("%1 %").arg(m_batch->percent()), true);
    else nav(m_shareNav)->setNote(m_copies.isEmpty() ? QString() : QString::number(m_copies.size()));
}
void MainWindow::setPage(Page page) {
    auto* target = m_pages->widget(page);
    if (page != Editor) m_editor->suspend();
    m_rail->setVisible(page != Editor); m_rail->setEnabled(page != Onboarding);
    m_pages->setCurrentWidget(target); m_toasts->setBottomInset((page == Library && !m_library->selectedPaths().isEmpty()) || (page == Sharing && !m_sharing->selectedPaths().isEmpty()) ? 88 : 20);
    updateNav(); updateCapture();
}
RecordingProfile& MainWindow::profile() {
    if (auto* active = m_config.activeProfile()) return *active;
    return m_config.profiles.first();
}
bool MainWindow::dirty() const { return m_config.toJson() != m_saved.toJson(); }
bool MainWindow::saveConfig(bool announce) {
    m_settings->collect();
    if (!m_loadError.isEmpty() && QFile::exists(m_store.filePath())) {
        const auto backup = m_store.filePath() + ".invalid-" + QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss-zzz");
        if (!QFile::copy(m_store.filePath(), backup)) { m_toasts->show(ToastHost::Error, QStringLiteral("Réglages non enregistrés"), QStringLiteral("Impossible de préserver l'ancien fichier de réglages. Aucun changement n'a été écrit.")); return false; }
    }
    QString error;
    if (!m_store.save(m_config, error)) { m_toasts->show(ToastHost::Error, QStringLiteral("Réglages non enregistrés"), error); return false; }
    const bool changedFolder = m_saved.libraryPath != m_config.libraryPath, changedShortcut = m_saved.startMenuShortcut != m_config.startMenuShortcut;
    m_loadError.clear(); m_saved = m_config; m_settings->setSaved(m_saved); setWindowModified(false);
    refreshTray(); spreadMix();
    if (changedShortcut && !m_programs.isEmpty() && !setStartMenuShortcut(m_programs, QCoreApplication::applicationFilePath(), m_saved.startMenuShortcut, error)) m_toasts->show(ToastHost::Warn, QStringLiteral("Menu Démarrer inchangé"), error);
    if (announce) m_toasts->show(ToastHost::Ok, QStringLiteral("Réglages enregistrés"), QStringLiteral("Ils s'appliquent aux prochains clips."));
    if (changedFolder) { refreshLibrary(); refreshSharing(); }
    updateCapture(); return true;
}
void MainWindow::revertConfig() {
    const bool folder = m_config.libraryPath != m_saved.libraryPath;
    m_config = m_saved; m_settings->load(); m_settings->setSaved(m_saved); setWindowModified(false); updateCapture();
    if (folder) { refreshLibrary(); refreshSharing(); }
}
void MainWindow::chooseLibraryFolder() {
    const auto state = m_replay->state();
    if (state == ReplayEngine::State::Starting || state == ReplayEngine::State::Running || state == ReplayEngine::State::Stopping) {
        m_toasts->show(ToastHost::Info, QStringLiteral("Le replay utilise ce dossier"), QStringLiteral("Arrêtez le replay avant de changer le dossier des clips.")); return;
    }
    const auto directory = QFileDialog::getExistingDirectory(this, QStringLiteral("Dossier des clips"), m_config.libraryPath);
    if (directory.isEmpty()) return;
    m_settings->setFolder(QDir::fromNativeSeparators(directory)); m_settings->collect(); setWindowModified(dirty()); m_library->setLoading(true); refreshLibrary(); refreshSharing();
}
void MainWindow::refreshLibrary() {
    m_library->setLibraryPath(m_config.libraryPath);
    if (m_scan->isRunning()) { m_rescanPending = true; return; }
    const auto directory = m_config.libraryPath; m_scan->setFuture(QtConcurrent::run([directory] { return scanLibrary(directory); }));
}
void MainWindow::rebuildLibrary() {
    QSet<QString> shared; for (const auto& copy : m_copies) if (!copy.link.source.isEmpty()) shared.insert(QDir::cleanPath(copy.link.source).toCaseFolded());
    QList<LibraryItem> items; items.reserve(m_clips.size());
    for (const auto& clip : m_clips) items.append({clip, m_labels.isFavorite(clip.path), m_fresh.contains(clip.path), shared.contains(QDir::cleanPath(clip.path).toCaseFolded())});
    m_library->setItems(items, m_config.discordLimitBytes); rebuildRail();
}
void MainWindow::refreshSharing() {
    if (m_shareScan->isRunning()) { m_shareRescanPending = true; return; }
    const auto directory = QDir(m_config.libraryPath).filePath("Partage Discord"); m_shareScan->setFuture(QtConcurrent::run([directory] { return scanLibrary(directory, true); }));
}
void MainWindow::updateBusy() {
    const bool trashing = m_trash->isRunning() || m_rename->isRunning(), sharing = m_batch->running();
    m_library->setBusy(trashing, sharing); m_sharing->setBusy(trashing); m_editor->setSharingBusy(sharing || trashing);
}
void MainWindow::savedFeedback(const QString& path) {
    if (m_config.clipSavedSound) playClipSavedSound();
    // The game usually has the focus at this moment: the system notification is the one that will be seen.
    if (m_config.clipSavedNotification && QSystemTrayIcon::isSystemTrayAvailable() && !isActiveWindow()) { m_tray->show(); m_tray->showMessage(QStringLiteral("Clip sauvegardé"), QFileInfo(path).completeBaseName(), QSystemTrayIcon::NoIcon, 4000); }
}
void MainWindow::startDiscordSharing(QStringList paths, bool edited) {
    if (m_shareDialog) { m_shareDialog->close(); m_shareDialog = nullptr; }
    if (!m_batch->running() && (paths.isEmpty() || m_trash->isRunning() || m_rename->isRunning() || m_editor->exporting())) return;
    std::optional<ClipInfo> info; std::optional<ClipEdit> edit;
    if (edited && !m_batch->running()) { if (!m_editor->ready()) return; info = m_editor->clipInfo(); edit = m_editor->currentEdit(); }
    // A running batch is shown rather than replaced: the dialog opens on its progress.
    if (m_batch->running()) paths.clear();
    const ShareOptions options{QDir(m_config.libraryPath).filePath("Partage Discord"), m_config.discordLimitBytes, m_config.discordCodec, m_config.discordAutoResize, m_saved.defaultMix};
    auto* dialog = new DiscordDialog(m_batch, m_cache, paths, options, this, info, edit); m_shareDialog = dialog; dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &DiscordDialog::preferencesChosen, this, [this](qint64 bytes, const QString& codec, bool resize) {
        m_config.discordLimitBytes = m_saved.discordLimitBytes = bytes; m_config.discordCodec = m_saved.discordCodec = codec; m_config.discordAutoResize = m_saved.discordAutoResize = resize;
        QString error; if (!m_store.save(m_saved, error)) m_toasts->show(ToastHost::Warn, QStringLiteral("Préférences de partage non enregistrées"), error);
        m_settings->setSaved(m_saved); rebuildLibrary();
    });
    connect(dialog, &DiscordDialog::showSharingRequested, this, [this] { setPage(Sharing); refreshSharing(); });
    connect(dialog, &DiscordDialog::copyRequested, this, &MainWindow::copyFiles);
    dialog->open();
}
void MainWindow::classifyClips(const QStringList& paths, const QString& game) {
    if (paths.isEmpty() || m_trash->isRunning()) return;
    QString error; const auto name = game.trimmed();
    if (!m_labels.setGames(paths, name, error)) { m_toasts->show(ToastHost::Error, QStringLiteral("Classement non enregistré"), error); return; }
    m_selectAfterScan = paths; refreshLibrary();
    const auto count = paths.size();
    m_toasts->show(ToastHost::Ok, name.isEmpty() ? (count > 1 ? QStringLiteral("%1 clips rendus à leur dossier d'origine").arg(count) : QStringLiteral("Clip rendu à son dossier d'origine")) : (count > 1 ? QStringLiteral("%1 clips classés dans « %2 »").arg(count).arg(name) : QStringLiteral("Clip classé dans « %1 »").arg(name)),
        QStringLiteral("Les fichiers ne sont pas déplacés."));
}
void MainWindow::deleteClips(QStringList paths, bool confirmed) {
    if (paths.isEmpty() || m_trash->isRunning() || m_rename->isRunning() || m_editor->exporting() || m_batch->running()) return;
    const bool copies = std::all_of(paths.begin(), paths.end(), [](const QString& path) { return path.endsWith(QStringLiteral(".discord.mp4"), Qt::CaseInsensitive); });
    if (!confirmed && !confirmTrash(this, paths, copies)) return;
    if (paths.contains(m_editor->clipPath())) { m_editor->suspend(); setPage(Library); }
    m_cache->setActive(false);
    const auto operation = m_trashOperation; auto analyses = m_editor->pendingAnalyses(); auto preview = m_cache->pending();
    m_trash->setFuture(QtConcurrent::run([paths, operation, analyses, preview]() mutable {
        // Readers may be finishing after cancellation. Waiting here keeps
        // file operations reliable without blocking the interface.
        for (auto& task : analyses) task.waitForFinished();
        if (preview.isStarted()) preview.waitForFinished();
        return trashClips(paths, operation);
    }));
    updateBusy();
}
void MainWindow::renameClip(const QString& path, const QString& name) {
    if (m_trash->isRunning() || m_rename->isRunning() || m_editor->exporting() || m_batch->running()) { m_toasts->show(ToastHost::Info, QStringLiteral("Renommage en attente"), QStringLiteral("Attendez la fin du traitement en cours.")); return; }
    // The editor holds the file open; it lets go for the time of the rename and opens the clip again after.
    if (m_pages->currentWidget() == m_editor && m_editor->clipPath() == path) { m_reopenAfterRename = path; m_editor->suspend(); }
    m_renaming = path; m_cache->setActive(false);
    auto analyses = m_editor->pendingAnalyses(); auto preview = m_cache->pending();
    m_rename->setFuture(QtConcurrent::run([path, name, analyses, preview]() mutable {
        for (auto& task : analyses) task.waitForFinished();
        if (preview.isStarted()) preview.waitForFinished();
        return renameClipFile(path, name);
    }));
    updateBusy();
}
void MainWindow::copyFiles(const QStringList& paths) {
    const auto urls = existingFileUrls(paths);
    if (urls.isEmpty()) { m_toasts->show(ToastHost::Warn, QStringLiteral("Fichiers introuvables"), QStringLiteral("Ils ont été déplacés ou supprimés.")); return; }
    auto* mime = new QMimeData; mime->setUrls(urls); QApplication::clipboard()->setMimeData(mime);
    m_toasts->show(ToastHost::Info, urls.size() > 1 ? QStringLiteral("%1 fichiers copiés").arg(urls.size()) : QStringLiteral("Fichier copié"), QStringLiteral("Collez dans Discord avec Ctrl V."), {}, QStringLiteral("copy"));
}
void MainWindow::revealFile(const QString& path) {
#ifdef Q_OS_WIN
    if (QFileInfo::exists(path)) { QProcess::startDetached(QStringLiteral("explorer.exe"), {QStringLiteral("/select,"), QDir::toNativeSeparators(path)}); return; }
#endif
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
}
void MainWindow::openClip(const QString& path) {
    if (path.isEmpty() || m_trash->isRunning() || m_rename->isRunning() || m_editor->exporting()) return;
    if (!QFileInfo(path).isFile()) { m_toasts->show(ToastHost::Warn, QStringLiteral("Clip introuvable"), QStringLiteral("Le fichier a été déplacé ou supprimé.")); refreshLibrary(); return; }
    m_fresh.remove(path); setPage(Editor); m_editor->openClip(path);
    QString game; for (const auto& clip : m_clips) if (clip.path == path) game = clip.game;
    if (game.isEmpty()) game = m_labels.gameFor(path, {});
    m_editor->setGame(game == unclassified ? QString() : game);
}
void MainWindow::viewClips(const QStringList& playlist, int index, bool fromEditor) {
    if (playlist.isEmpty() || index < 0 || index >= playlist.size() || m_trash->isRunning() || m_rename->isRunning()) return;
    if (!QFileInfo(playlist[index]).isFile()) { m_toasts->show(ToastHost::Warn, QStringLiteral("Clip introuvable"), QStringLiteral("Le fichier a été déplacé ou supprimé.")); refreshLibrary(); return; }
    if (!m_viewer) {
        m_viewer = new ClipViewer(this); m_viewer->setVolume(m_saved.playbackVolume); m_viewer->setDefaultMix(m_saved.defaultMix);
        connect(m_viewer, &ClipViewer::viewed, this, [this](const QString& path) { if (m_fresh.remove(path)) rebuildLibrary(); });
        // Back from the full screen, the editor is where the viewing stopped.
        connect(m_viewer, &ClipViewer::closed, this, [this](const QString& path, qint64 position) {
            if (m_pages->currentWidget() == m_editor && m_editor->clipPath() == path) m_editor->showPosition(position);
            // The level chosen while watching is the one the next viewing starts with.
            if (m_viewer->volume() == m_saved.playbackVolume) return;
            m_config.playbackVolume = m_saved.playbackVolume = m_viewer->volume();
            // A settings file that could not be read is left as it is until the settings are saved on purpose.
            if (!m_loadError.isEmpty()) return;
            QString error; if (!m_store.save(m_saved, error)) m_toasts->show(ToastHost::Error, QStringLiteral("Volume non enregistré"), error);
            m_settings->setSaved(m_saved);
        });
    }
    if (fromEditor) { if (!m_editor->ready()) return; m_editor->pausePlayback(); m_viewer->open(playlist, index, m_editor->currentEdit(), m_editor->position()); }
    else m_viewer->open(playlist, index);
}
void MainWindow::followUpdates(const QString& repository, const QUrl& feed) {
    if (feed.isValid()) m_updates->setFeed(feed); else m_updates->setRepository(repository);
    m_settings->setUpdates(m_updates->enabled(), m_updates->currentVersion());
    // The first launch is for the welcome page; the question waits for the next one.
    if (m_updates->enabled() && m_saved.checkUpdates && !m_firstLaunch) QTimer::singleShot(feed.isValid() ? 0 : 4000, m_updates, [this] { m_updates->check(); });
}
void MainWindow::announceUpdated(const QString& previousVersion) {
    m_toasts->show(ToastHost::Ok, QStringLiteral("Clipsapp est à jour"), QStringLiteral("Version %1 installée, à la place de la %2.").arg(m_updates->currentVersion(), previousVersion));
}
void MainWindow::refreshUpdateButton() {
    using State = UpdateService::State; const auto state = m_updates->state(); const auto version = m_updates->info().version.toString();
    QString text;
    if (state == State::Available || state == State::Failed) text = QStringLiteral("Mise à jour %1").arg(version);
    else if (state == State::Downloading) text = QStringLiteral("Téléchargement %1 %").arg(qRound(m_updates->progress() * 100));
    else if (state == State::Unpacking) text = QStringLiteral("Préparation…");
    else if (state == State::Ready) text = QStringLiteral("Redémarrer · %1").arg(version);
    // Checking again does not take away an update already found.
    else if (state == State::Checking && m_updateButton->isVisible()) return;
    if (!text.isEmpty() && text != m_updateButton->text()) m_updateButton->setText(text);
    m_updateButton->setVisible(!text.isEmpty());
    m_updateButton->setToolTip(state == State::Ready ? QStringLiteral("La nouvelle version est prête : redémarrer Clipsapp pour l'utiliser") : QStringLiteral("Voir ce qu'apporte la nouvelle version"));
}
void MainWindow::showUpdate() {
    using State = UpdateService::State;
    if (m_updates->info().version.isNull() || findChild<QDialog*>("update-dialog")) return;
    ModalDialog dialog(this, 520); dialog.setObjectName("update-dialog");
    dialog.setHeading(QStringLiteral("Clipsapp %1 est disponible").arg(m_updates->info().version.toString()), QStringLiteral("Vous utilisez la version %1.").arg(m_updates->currentVersion()), new IconBox(QStringLiteral("save"), tk::accent, tk::accentSoft));
    dialog.body()->setSpacing(12);
    if (!m_updates->info().notes.isEmpty()) {
        // What the release says about itself, as written on its page.
        auto* notes = label({}, nullptr, true); notes->setObjectName("update-notes"); notes->setTextFormat(Qt::MarkdownText); notes->setText(m_updates->info().notes); notes->setOpenExternalLinks(false); notes->setAlignment(Qt::AlignTop | Qt::AlignLeft); notes->setContentsMargins(12, 8, 12, 8);
        auto* well = card("well"); auto* wellLayout = new QVBoxLayout(well); wellLayout->setContentsMargins(1, 1, 1, 1);
        auto* scroll = new QScrollArea; scroll->setWidgetResizable(true); scroll->setWidget(notes); scroll->setFrameShape(QFrame::NoFrame); scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); scroll->setFixedHeight(190);
        wellLayout->addWidget(scroll); dialog.body()->addWidget(well);
    }
    auto* status = label({}, "small", true); status->setObjectName("update-status"); dialog.body()->addWidget(status);
    auto* bar = new Bar; bar->setObjectName("update-progress"); dialog.body()->addWidget(bar);
    auto* page = new Button(QStringLiteral("open"), QStringLiteral("Page de la version"), Button::Ghost); page->setVisible(m_updates->info().page.isValid()); dialog.footerLeft()->addWidget(page);
    auto* stop = new Button(QStringLiteral("Annuler le téléchargement"), Button::Ghost); stop->setObjectName("update-cancel");
    auto* later = new Button(QStringLiteral("Plus tard")); later->setObjectName("update-later");
    auto* action = new Button(QString(), Button::Primary); action->setObjectName("update-action");
    dialog.footer()->addWidget(stop); dialog.footer()->addWidget(later); dialog.footer()->addWidget(action);
    const auto refresh = [this, status, bar, stop, later, action, &dialog] {
        const auto state = m_updates->state(); const auto& info = m_updates->info(); const bool working = state == State::Downloading || state == State::Unpacking;
        // Closing the dialog never stops a download: it goes on, and the side bar shows where it is.
        bar->setVisible(working); stop->setVisible(working); action->setVisible(!working && state != State::Idle && state != State::Checking); later->setText(working ? QStringLiteral("Continuer en arrière-plan") : QStringLiteral("Plus tard"));
        status->setStyleSheet(state == State::Failed ? QStringLiteral("color: %1;").arg(tk::danger.name()) : QString());
        if (state == State::Available) { status->setText(QStringLiteral("Téléchargement de %1 depuis GitHub, puis un redémarrage de Clipsapp. Vos clips et vos réglages ne sont pas touchés.").arg(formatBytes(info.bytes))); action->setText(QStringLiteral("Télécharger")); }
        else if (state == State::Downloading) { status->setText(QStringLiteral("Téléchargement : %1 sur %2").arg(formatBytes(m_updates->receivedBytes()), formatBytes(info.bytes))); bar->setValue(m_updates->progress()); }
        else if (state == State::Unpacking) { status->setText(QStringLiteral("Archive vérifiée. Préparation de la nouvelle version…")); bar->setValue(1); }
        else if (state == State::Ready) {
            const bool replay = m_replay->state() == ReplayEngine::State::Running || m_replay->state() == ReplayEngine::State::Starting;
            status->setText(QStringLiteral("La version %1 est prête. Clipsapp doit redémarrer pour l'utiliser.").arg(info.version.toString()) + (replay ? QStringLiteral(" Le replay s'arrêtera : sauvegardez d'abord le clip en cours si vous y tenez.") : QString()));
            action->setText(QStringLiteral("Redémarrer et installer"));
        } else if (state == State::Failed) { status->setText(m_updates->error()); action->setText(QStringLiteral("Réessayer")); }
        else if (state == State::Idle) dialog.reject();
        dialog.refit();
    };
    connect(m_updates, &UpdateService::changed, &dialog, refresh); refresh();
    connect(page, &QPushButton::clicked, &dialog, [this] { QDesktopServices::openUrl(m_updates->info().page); });
    connect(stop, &QPushButton::clicked, &dialog, [this] { m_updates->cancel(); });
    connect(later, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(action, &QPushButton::clicked, &dialog, [this, &dialog] { if (m_updates->state() == State::Ready) dialog.done(2); else m_updates->download(); });
    later->setDefault(true); later->setFocus();
    if (dialog.exec() == 2) installUpdate();
}
void MainWindow::installUpdate() {
    // Nothing is replaced while files are being written or settings wait for an answer: the restart must have nothing to ask.
    if (m_trash->isRunning() || m_rename->isRunning()) { m_toasts->show(ToastHost::Info, QStringLiteral("Un instant"), QStringLiteral("Des fichiers sont en cours de traitement.")); return; }
    if (m_batch->running() || m_editor->exporting()) { m_toasts->show(ToastHost::Warn, QStringLiteral("Mise à jour reportée"), QStringLiteral("Un export ou un lot de copies Discord est en cours. Relancez l'installation quand il sera terminé.")); return; }
    if (dirty()) { m_toasts->show(ToastHost::Warn, QStringLiteral("Mise à jour reportée"), QStringLiteral("Des réglages ne sont pas enregistrés. Enregistrez-les ou annulez-les, puis relancez l'installation.")); return; }
    QString error; if (!m_updates->install(error)) { m_toasts->show(ToastHost::Error, QStringLiteral("Mise à jour non installée"), error); return; }
    m_relaunch = true; m_restartPending = false; m_replay->stop(); close();
}
void MainWindow::spreadMix() { m_editor->setDefaultMix(m_saved.defaultMix); if (m_viewer) m_viewer->setDefaultMix(m_saved.defaultMix); }
void MainWindow::adoptMix(const TrackMix& mix) {
    // The habit starts again from the new levels: what led to them, and what was refused, is forgotten.
    m_config.defaultMix = m_saved.defaultMix = mix; m_config.mixHistory = m_saved.mixHistory = {}; m_config.mixDeclined = m_saved.mixDeclined = {}; m_mixAsked.clear();
    QString error; if (m_loadError.isEmpty() && !m_store.save(m_saved, error)) m_toasts->show(ToastHost::Warn, QStringLiteral("Volumes habituels non enregistrés"), error);
    m_settings->load(); m_settings->setSaved(m_saved); spreadMix();
}
QString MainWindow::mixText(const TrackMix& from, const TrackMix& to) const {
    QStringList parts;
    for (auto track = to.cbegin(); track != to.cend(); ++track) {
        if (std::abs(track.value() - from.value(track.key(), 1)) < 0.01) continue;
        // Tracks are kept by their folded name: the profile gives back the way it is written.
        QString name = track.key(); for (const auto& known : m_saved.profiles) for (const auto& candidate : known.tracks) if (mixKey(candidate.name) == track.key()) name = candidate.name;
        parts.append(QStringLiteral("%1 à %2 %").arg(name).arg(qRound(track.value() * 100)));
    }
    return parts.size() > 1 ? parts.mid(0, parts.size() - 1).join(QStringLiteral(", ")) + QStringLiteral(" et ") + parts.last() : parts.join(QString());
}
void MainWindow::learnMix(const TrackMix& levels) {
    if (levels.isEmpty() || !m_loadError.isEmpty()) return;
    rememberExport(m_saved.mixHistory, levels); m_config.mixHistory = m_saved.mixHistory;
    QString error; m_store.save(m_saved, error); m_settings->setSaved(m_saved);
    const auto proposal = suggestedMix(m_saved.mixHistory, m_saved.defaultMix, m_saved.mixDeclined);
    // Asked once: a question left unanswered is not put again before the habit changes.
    if (!proposal || (!m_mixAsked.isEmpty() && sameMix(*proposal, m_mixAsked))) return;
    m_mixAsked = *proposal; const auto mix = *proposal;
    m_toasts->show(ToastHost::Info, QStringLiteral("En faire vos volumes habituels ?"), QStringLiteral("Vous réglez souvent %1. Les prochains clips s'ouvriraient ainsi ; les enregistrements ne changent pas.").arg(mixText(m_saved.defaultMix, mix)), {
        {QStringLiteral("Appliquer"), [this, mix] { adoptMix(mix); }},
        {QStringLiteral("Non merci"), [this, mix] {
            m_config.mixDeclined = m_saved.mixDeclined = mix; m_config.mixHistory = m_saved.mixHistory = {};
            QString failure; if (m_loadError.isEmpty()) m_store.save(m_saved, failure); m_settings->setSaved(m_saved);
        }}}, QStringLiteral("speaker"));
}
void MainWindow::manageStartMenu(const QString& programsDirectory) {
    m_programs = programsDirectory; m_settings->setStartMenu(!m_programs.isEmpty()); m_onboarding->setStartMenu(!m_programs.isEmpty());
    // A folder that was moved leaves its shortcut pointing nowhere: it is drawn again. One that
    // leads to another copy of the application is that copy's own, and is left alone.
    QString error; if (!m_programs.isEmpty() && m_saved.startMenuShortcut && !m_firstLaunch && !startMenuShortcutWorks(m_programs)) setStartMenuShortcut(m_programs, QCoreApplication::applicationFilePath(), true, error);
}
void MainWindow::quitWhenClosed() { m_quitOnClose = true; }
void MainWindow::quit() { m_quitting = true; close(); }
void MainWindow::surface() {
    setWindowState(windowState() & ~Qt::WindowMinimized); show(); raise(); activateWindow();
}
void MainWindow::refreshTray() {
    const auto state = m_replay->state(); const bool running = state == ReplayEngine::State::Running, busy = running || state == ReplayEngine::State::Starting;
    m_traySave->setEnabled(running && m_replay->stats().seconds > 0 && !m_replay->exporting());
    m_trayToggle->setText(busy ? QStringLiteral("Arrêter le replay") : QStringLiteral("Démarrer le replay"));
    m_trayToggle->setEnabled(state != ReplayEngine::State::Stopping && m_pages->currentWidget() != m_onboarding);
    m_tray->setToolTip(busy ? QStringLiteral("Clipsapp · replay en cours") : QStringLiteral("Clipsapp · replay arrêté"));
    // The icon is there from the moment the window is on screen, since the window may then hide
    // behind it. Otherwise it only comes with the notification of a saved clip.
    if (m_saved.closeToTray && QSystemTrayIcon::isSystemTrayAvailable() && !testAttribute(Qt::WA_DontShowOnScreen)) { if (isVisible()) m_tray->show(); }
    else if (!m_saved.clipSavedNotification) m_tray->hide();
}
void MainWindow::showEvent(QShowEvent* event) { QMainWindow::showEvent(event); refreshTray(); }
void MainWindow::closeEvent(QCloseEvent* event) {
    // Closing the window leaves the replay running behind the notification area. Only "Quitter",
    // in the menu of its icon, or an update being installed, ends the application from there.
    // The welcome page is not left that way: a first launch that is closed starts again.
    const bool quitting = std::exchange(m_quitting, false) || m_relaunch;
    if (!quitting && !m_firstLaunch && m_saved.closeToTray && m_tray->isVisible()) {
        event->ignore(); if (m_editor->ready()) m_editor->pausePlayback(); hide();
        if (!m_saved.trayNoticeShown) {
            m_tray->showMessage(QStringLiteral("Clipsapp reste ouvert"), QStringLiteral("Le replay continue ici, près de l'horloge. Pour fermer l'application : clic droit sur l'icône, puis Quitter."), QSystemTrayIcon::NoIcon, 8000);
            m_saved.trayNoticeShown = m_config.trayNoticeShown = true; m_settings->setSaved(m_saved);
            QString error; if (m_loadError.isEmpty()) m_store.save(m_saved, error);
        }
        return;
    }
    // Asked to quit from its icon, a hidden window comes back only if it has something to say.
    if (isHidden() && (m_trash->isRunning() || m_rename->isRunning() || m_batch->running() || dirty())) surface();
    if (m_trash->isRunning() || m_rename->isRunning()) { m_toasts->show(ToastHost::Info, QStringLiteral("Un instant"), QStringLiteral("Des fichiers sont en cours de traitement.")); event->ignore(); return; }
    if (m_batch->running() && choose(this, "quit-batch-prompt", QStringLiteral("Un lot de copies Discord est en cours"), QStringLiteral("Quitter l'arrête. Les copies déjà prêtes sont conservées."), {QStringLiteral("Continuer le lot"), QStringLiteral("Quitter quand même")}, -1, 0) != 1) { event->ignore(); return; }
    if (dirty()) {
        const int choice = choose(this, "unsaved-settings-prompt", QStringLiteral("Des réglages ne sont pas enregistrés"), QStringLiteral("Les enregistrer avant de fermer ?"), {QStringLiteral("Annuler"), QStringLiteral("Ne pas enregistrer"), QStringLiteral("Enregistrer")}, 2, 2);
        if (choice <= 0 || (choice == 2 && !saveConfig(false))) { event->ignore(); return; }
    }
    m_tray->hide(); event->accept();
    // A window hidden behind its icon is not counted by Qt when it closes: the application ends here.
    if (m_quitOnClose) QCoreApplication::quit();
}
QJsonObject MainWindow::smokeReport() const {
    const auto& catalog = m_settings->catalog();
    return {{"version", CLIPSAPP_VERSION}, {"configPath", m_store.filePath()}, {"profiles", m_config.profiles.size()}, {"microphones", catalog.microphones.size()}, {"applications", catalog.applications.size()}, {"outputs", catalog.outputs.size()}, {"videoTargets", catalog.videoTargets.size()}, {"captureImplemented", ReplayEngine::supported()}, {"configurationError", m_loadError}};
}
void MainWindow::toggleCapture() {
    const auto state = m_replay->state();
    if (state == ReplayEngine::State::Starting || state == ReplayEngine::State::Running) m_replay->stop(); else startCapture();
}
void MainWindow::startCapture() {
    const auto state = m_replay->state();
    if (state == ReplayEngine::State::Starting || state == ReplayEngine::State::Running || state == ReplayEngine::State::Stopping) return;
    if (!saveConfig(false)) return;
    QString error; m_replayError.clear();
    if (!m_globalHotkey->bind(m_settings->hotkey(), error) || !m_replay->start(profile(), m_config.libraryPath, error)) {
        m_globalHotkey->unbind();
        m_toasts->show(ToastHost::Error, QStringLiteral("Le replay n'a pas démarré"), error, {{QStringLiteral("Voir les réglages"), [this] { m_settings->showTab(0); setPage(Settings); }}});
    }
    updateCapture();
}
void MainWindow::saveReplay() {
    QString error; if (!m_replay->save(error)) m_toasts->show(ToastHost::Warn, QStringLiteral("Rien à sauvegarder pour l'instant"), error);
}
void MainWindow::captureWholeScreen() {
    // Capturing a window makes Windows draw its yellow border; a whole screen has none.
    const SourceTarget* screen = nullptr;
    for (const auto& target : m_settings->catalog().videoTargets) if (target.id.startsWith(QLatin1String("screen:")) && (!screen || target.label.endsWith(QStringLiteral("principal")))) screen = &target;
    if (!screen) { m_toasts->show(ToastHost::Warn, QStringLiteral("Aucun écran disponible"), QStringLiteral("Choisissez la cible dans Réglages › Capture.")); return; }
    profile().video.targetId = screen->id; profile().video.targetLabel = screen->label;
    m_settings->load(); m_restartPending = true; m_replay->stop();
    m_toasts->show(ToastHost::Info, QStringLiteral("Capture de l'écran entier"), QStringLiteral("Le replay redémarre sur %1. Le buffer repart de zéro.").arg(targetName(profile().video)));
}
QString MainWindow::targetName(const VideoSettings& video) const {
    if (video.targetId.isEmpty()) return QStringLiteral("aucune cible choisie");
    if (video.targetId.startsWith(QLatin1String("screen:"))) {
        static const QRegularExpression digits(QStringLiteral("(\\d+)\\D*$")); const auto match = digits.match(video.targetId);
        return match.hasMatch() ? QStringLiteral("Écran %1").arg(match.captured(1)) : QStringLiteral("Écran");
    }
    return video.targetLabel.section(QStringLiteral(" · "), 0, 0).left(40);
}
void MainWindow::updateCapture() {
    const auto state = m_replay->state(); const auto stats = m_replay->stats(); const auto& p = profile();
    const bool busy = state == ReplayEngine::State::Starting || state == ReplayEngine::State::Running || state == ReplayEngine::State::Stopping;
    if (state == ReplayEngine::State::Idle && m_restartPending) { m_restartPending = false; QMetaObject::invokeMethod(this, [this] { startCapture(); }, Qt::QueuedConnection); }
    m_settings->setLocked(busy);
    if (!busy) m_globalHotkey->unbind();
    QSet<QString> tracks; for (const auto& source : p.sources) if (source.enabled) tracks.insert(source.trackId);
    ReplayView view; view.capacity = p.replay.durationSeconds; view.hotkey = p.replay.hotkey; view.seconds = stats.seconds; view.bytes = stats.bytes; view.saving = m_replay->exporting();
    view.state = state == ReplayEngine::State::Starting ? ReplayView::Starting : state == ReplayEngine::State::Running ? ReplayView::Running : state == ReplayEngine::State::Stopping ? ReplayView::Stopping : state == ReplayEngine::State::Error ? ReplayView::Error : ReplayView::Idle;
    view.profileSummary = QStringLiteral("Profil %1 · %2 · %3 dernières secondes · %4 piste%5").arg(p.name, targetName(p.video)).arg(p.replay.durationSeconds).arg(tracks.size()).arg(tracks.size() > 1 ? "s" : "");
    const auto format = QStringLiteral("%1p %2 FPS").arg(p.video.height).arg(p.video.fps);
    if (state == ReplayEngine::State::Starting) view.liveSummary = QStringLiteral("Préparation de l'encodeur · %1").arg(targetName(p.video));
    else { const auto game = m_replay->clipGameName(); QStringList parts; if (!game.isEmpty()) parts << (p.gameName.trimmed().isEmpty() && p.detectGame && game != p.name ? game + QStringLiteral(" détecté") : game); parts << format; if (!stats.encoder.isEmpty()) parts << stats.encoder; view.liveSummary = parts.join(QStringLiteral(" · ")); }
    view.warning = stats.warning; view.warningTitle = QStringLiteral("bordure Windows visible"); view.canFixWarning = p.video.targetId.startsWith(QLatin1String("window:"));
    // Images given up come before the border: a clip that stutters is the worse of the two.
    if (stats.lost > 0.02) {
        view.warningTitle = QStringLiteral("images perdues"); view.canFixWarning = false;
        view.warning = QStringLiteral("L'encodeur ne suit pas à %1 FPS : %2 % des images sont perdues. Baissez la fréquence, la résolution ou l'effort d'encodage.").arg(p.video.fps).arg(qRound(stats.lost * 100));
    }
    view.error = m_replayError.isEmpty() ? QStringLiteral("La capture s'est interrompue. Les clips déjà sauvegardés ne sont pas touchés.") : m_replayError;
    const bool onboarding = m_pages->currentWidget() == m_onboarding;
    view.canStart = ReplayEngine::supported() && !m_replay->exporting() && !onboarding; view.canSave = state == ReplayEngine::State::Running && stats.seconds > 0;
    view.blockedReason = !ReplayEngine::supported() ? QStringLiteral("La capture Linux arrivera avec PipeWire et le portail KDE.") : onboarding ? QStringLiteral("Terminez la configuration pour démarrer") : QString();
    m_strip->apply(view); refreshTray();
    m_library->setReplayHint(QKeySequence::fromString(p.replay.hotkey, QKeySequence::PortableText), p.replay.durationSeconds, view.canStart, busy);
}
bool MainWindow::smokeCheck(QString& error) {
    const int originalFps = profile().video.fps, probe = originalFps == 30 ? 60 : 30;
    m_settings->setFps(probe); m_settings->collect();
    if (profile().video.fps != probe || !dirty()) { error = QStringLiteral("Le formulaire ne met pas à jour le profil."); return false; }
    if (!m_store.save(m_config, error)) return false;
    AppConfig loaded; if (!m_store.load(loaded, error) || loaded.activeProfile()->video.fps != probe) { error = QStringLiteral("Le formulaire sauvegardé n'a pas été retrouvé."); return false; }
    m_settings->setFps(originalFps); m_settings->collect();
    if (!m_store.save(m_config, error)) return false;
    m_saved = m_config; m_settings->setSaved(m_saved); setWindowModified(false);
    return true;
}
bool MainWindow::writePreviews(const QString& directory, QString& error) {
    if (!QDir().mkpath(directory)) { error = QStringLiteral("Impossible de créer le dossier des aperçus."); return false; }
    const auto capture = [this, &directory, &error](const QString& name) {
        QApplication::processEvents();
        if (!grab().save(QDir(directory).filePath(name), "PNG")) { error = QStringLiteral("Impossible d'écrire l'aperçu : ") + name; return false; }
        return true;
    };
    const auto current = Page(m_pages->currentIndex());
    setPage(Library); if (!capture("library.png")) return false;
    setPage(Sharing); if (!capture("sharing.png")) return false;
    setPage(Settings); m_settings->showTab(0); if (!capture("settings-capture.png")) return false;
    m_settings->showTab(1); if (!capture("settings-audio.png")) return false;
    m_settings->showTab(2); if (!capture("settings-files.png")) return false;
    m_settings->showTab(0); setPage(Onboarding); if (!capture("onboarding.png")) return false;
    setPage(current == Editor ? Library : current);
    return true;
}
}
