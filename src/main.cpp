#include <QApplication>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QIcon>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <cstdio>
#include "core/app_log.h"
#include "core/config_store.h"
#include "core/update.h"
#include "platform/crash_report.h"
#include "platform/game_detection.h"
#include "platform/login_start.h"
#include "platform/system_info.h"
#include "platform/uninstall.h"
#include "ui/main_window.h"
#include "ui/update_service.h"
#include "ui/theme.h"
#include <QElapsedTimer>
#include <QThread>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

int main(int argc, char** argv) {
#ifdef Q_OS_WIN
    // A published copy keeps everything but its executable in "runtime": Windows finds the libraries
    // there through the manifest of the executable, and Qt its plugins through this path, given
    // before the application starts loading them. A build made from the sources has no such folder.
    {
        wchar_t executable[32768]; const DWORD length = GetModuleFileNameW(nullptr, executable, 32768);
        const auto runtime = QFileInfo(QString::fromWCharArray(executable, int(length))).dir().filePath(QStringLiteral("runtime"));
        if (length && QFileInfo(runtime).isDir()) QCoreApplication::addLibraryPath(runtime);
    }
#endif
    QApplication application(argc, argv);
    application.setApplicationName("Clipsapp"); application.setOrganizationName("Clipsapp"); application.setApplicationVersion(CLIPSAPP_VERSION);
    QCommandLineParser parser; parser.setApplicationDescription(QStringLiteral("Bibliothèque locale et configuration de clips vidéo")); parser.addHelpOption(); parser.addVersionOption();
    parser.addOption({"data-dir", QStringLiteral("Dossier de configuration alternatif"), "directory"});
    parser.addOption({"smoke-test", QStringLiteral("Vérifier le démarrage et la sauvegarde, puis quitter")});
    parser.addOption({"previews-dir", QStringLiteral("Dossier des captures de l'interface en mode smoke-test"), "directory"});
    parser.addOption({"updated-from", QStringLiteral("Version remplacée par une mise à jour qui vient d'être installée"), "version"});
    parser.addOption({"quit", QStringLiteral("Fermer la copie de Clipsapp en cours, comme « Quitter » dans le menu de son icône")});
    parser.addOption({"autostart", QStringLiteral("Lancement à l'ouverture de la session : attendre dans la zone de notification")});
    parser.addOption({"uninstall", QStringLiteral("Fermer Clipsapp et retirer ce qu'il laisse hors de son dossier : raccourci, lancement avec la session, réglages. Utilisé par « Desinstaller Clipsapp.bat »")});
    parser.addOption({"keep-settings", QStringLiteral("Avec --uninstall : garder les réglages pour une prochaine installation")}); parser.process(application);
    const bool smoke = parser.isSet("smoke-test");
    if (smoke && !parser.isSet("data-dir")) { std::fprintf(stderr, "Smoke tests require --data-dir to isolate settings.\n"); return 2; }
    const QString configDirectory = parser.isSet("data-dir") ? QDir(parser.value("data-dir")).absolutePath() : clipsapp::defaultConfigDirectory();
    // Up to 0.8.2, Qt's own folder put the settings in "Clipsapp\Clipsapp": they come up one level, before anything reads them.
    if (!parser.isSet("data-dir")) clipsapp::adoptEarlierSettings(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation), configDirectory);
    if (application.platformName() == "offscreen") {
#ifdef Q_OS_WIN
        const QDir fontDirectory(QDir(qEnvironmentVariable("WINDIR", "C:/Windows")).filePath("Fonts"));
        QFontDatabase::addApplicationFont(fontDirectory.filePath("segoeui.ttf"));
        QFontDatabase::addApplicationFont(fontDirectory.filePath("segoeuib.ttf"));
#endif
    }
    clipsapp::applyTheme(application);
    application.setWindowIcon(clipsapp::appIcon());
    const QString installDirectory = QCoreApplication::applicationDirPath();
    // Only a published copy of the application follows the project's releases: it carries the list
    // of its files. A build made from the sources is left as its developer made it.
    const bool published = clipsapp::publishedCopy(installDirectory);
    // One copy runs per set of settings. Started again while its window waits in the notification
    // area, from the Start menu for instance, the application only asks that copy to show itself.
    const QString instance = QStringLiteral("Clipsapp-") + QString::fromLatin1(QCryptographicHash::hash(configDirectory.toCaseFolded().toUtf8(), QCryptographicHash::Sha1).toHex().left(16));
    // Asked by the uninstall script: the copy that runs closes as with "Quitter", then what the
    // application left outside its folder goes. The script reads the outcome and deletes the folder.
    if (parser.isSet("uninstall")) {
        const clipsapp::UninstallPlan plan{QCoreApplication::applicationDirPath(), QCoreApplication::applicationFilePath(), configDirectory, QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation), clipsapp::loginStartLocation(), parser.isSet("keep-settings")};
        // Clips kept inside the folder: the replay that runs is not even stopped.
        if (!clipsapp::clipsInside(plan).isEmpty()) return clipsapp::UninstallClipsInside;
        QLocalSocket other; other.connectToServer(instance);
        if (other.waitForConnected(300)) { other.write("quit"); other.waitForBytesWritten(300); other.disconnectFromServer(); }
        const auto others = [&plan] { auto processes = clipsapp::programProcesses(plan.executable); processes.removeAll(QCoreApplication::applicationPid()); return processes; };
        // A copy that asks about unsaved settings, or a batch of Discord copies, is left to the user.
        QElapsedTimer waiting; waiting.start();
        while (!others().isEmpty() && waiting.elapsed() < 15000) QThread::msleep(200);
        if (!others().isEmpty()) return clipsapp::UninstallStillRunning;
        return clipsapp::removeTraces(plan).code;
    }
    QLocalServer running; running.setSocketOptions(QLocalServer::UserAccessOption);
    if (!smoke) {
        QLocalSocket other; other.connectToServer(instance);
        if (other.waitForConnected(300)) { other.write(parser.isSet("quit") ? "quit" : "show"); other.waitForBytesWritten(300); return 0; }
        if (parser.isSet("quit")) return 0;
        running.listen(instance);
    }
    // The journal and the crash reports live beside the settings. A smoke test leaves no trace there.
    const QString logDirectory = QDir(configDirectory).filePath(QStringLiteral("logs"));
    const bool atLogin = parser.isSet("autostart");
    if (!smoke) {
        clipsapp::installAppLog(logDirectory); clipsapp::installCrashReports(logDirectory);
        qInfo("Clipsapp %s démarre%s. %s · %s", CLIPSAPP_VERSION, atLogin ? " avec la session" : "", qUtf8Printable(clipsapp::systemDescription()), qUtf8Printable(clipsapp::mediaLibraries()));
        for (const auto& adapter : clipsapp::graphicsAdapters()) qInfo("Carte graphique : %s", qUtf8Printable(adapter));
    }
    int code = 0; bool relaunch = false;
    {
        clipsapp::MainWindow window(configDirectory);
        if (smoke) window.setAttribute(Qt::WA_DontShowOnScreen, true);
        else { window.manageStartMenu(QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation)); window.manageLoginStart(clipsapp::loginStartLocation()); }
        // The copy started again says what it wants: to see the window, or the application to end.
        QObject::connect(&running, &QLocalServer::newConnection, &window, [&running, &window] {
            while (auto* asking = running.nextPendingConnection()) {
                if (!asking->bytesAvailable()) asking->waitForReadyRead(200);
                const bool quit = asking->readAll() == "quit"; asking->deleteLater();
                if (quit) window.quit(); else window.surface();
            }
        });
        // The application lasts as long as its window, even hidden behind its icon: not as long as the last window on screen.
        if (!smoke) { application.setQuitOnLastWindowClosed(false); window.quitWhenClosed(); }
        // Started with the session, the window stays behind its icon, unless the settings ask for it in front
        // or there is no icon; then it comes to the front.
        if (!(atLogin && !smoke && window.startInTray())) { window.show(); if (atLogin) window.surface(); }
        if (!smoke) {
            window.beginAutoReplay(atLogin); window.armShortcuts();
            if (const auto report = clipsapp::unseenCrashReport(logDirectory); !report.isEmpty()) window.announceCrash(report);
        }
        if (!smoke && published) {
            window.followUpdates(QStringLiteral(CLIPSAPP_UPDATE_REPOSITORY));
            if (parser.isSet("updated-from")) window.announceUpdated(parser.value("updated-from"));
            // What the previous version left behind when it was replaced, once it has closed for good.
            // Not while another update is being fetched into that same folder.
            QTimer::singleShot(5000, &window, [&window, installDirectory] {
                using State = clipsapp::UpdateService::State; const auto state = window.updates()->state();
                if (state == State::Idle || state == State::Checking || state == State::Available) clipsapp::clearUpdateLeftovers(installDirectory);
            });
        }
        if (smoke) QTimer::singleShot(300, &window, [&application, &window, &parser, configDirectory] {
            QString error;
            bool passed = window.smokeCheck(error);
            if (passed && parser.isSet("previews-dir")) passed = window.writePreviews(parser.value("previews-dir"), error);
            auto report = window.smokeReport(); report.insert("passed", passed); report.insert("error", error);
            const auto bytes = QJsonDocument(report).toJson(QJsonDocument::Indented);
            QFile reportFile(QDir(configDirectory).filePath("smoke-report.json"));
            if (!reportFile.open(QIODevice::WriteOnly) || reportFile.write(bytes) != bytes.size()) { std::fprintf(stderr, "Unable to write smoke report.\n"); application.exit(1); return; }
            std::fwrite(bytes.constData(), 1, static_cast<size_t>(bytes.size()), stdout);
            application.exit(passed ? 0 : 1);
        });
        code = application.exec(); relaunch = window.relaunchRequested();
    }
    if (!smoke) qInfo("Clipsapp se ferme%s.", relaunch ? " pour sa mise à jour" : "");
    // The window is gone, with its capture and its global shortcut: the new version can take over.
    running.close();
    if (relaunch) {
        QStringList arguments{QStringLiteral("--updated-from"), QStringLiteral(CLIPSAPP_VERSION)};
        if (parser.isSet("data-dir")) arguments << QStringLiteral("--data-dir") << configDirectory;
        QProcess::startDetached(QCoreApplication::applicationFilePath(), arguments, installDirectory);
    }
    return code;
}
