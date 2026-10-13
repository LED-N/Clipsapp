#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QIcon>
#include <QJsonDocument>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <cstdio>
#include "core/update.h"
#include "ui/main_window.h"
#include "ui/update_service.h"
#include "ui/theme.h"

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    application.setApplicationName("Clipsapp"); application.setOrganizationName("Clipsapp"); application.setApplicationVersion(CLIPSAPP_VERSION);
    QCommandLineParser parser; parser.setApplicationDescription(QStringLiteral("Bibliothèque locale et configuration de clips vidéo")); parser.addHelpOption(); parser.addVersionOption();
    parser.addOption({"data-dir", QStringLiteral("Dossier de configuration alternatif"), "directory"});
    parser.addOption({"smoke-test", QStringLiteral("Vérifier le démarrage et la sauvegarde, puis quitter")});
    parser.addOption({"previews-dir", QStringLiteral("Dossier des captures de l'interface en mode smoke-test"), "directory"});
    parser.addOption({"updated-from", QStringLiteral("Version remplacée par une mise à jour qui vient d'être installée"), "version"}); parser.process(application);
    const bool smoke = parser.isSet("smoke-test");
    if (smoke && !parser.isSet("data-dir")) { std::fprintf(stderr, "Smoke tests require --data-dir to isolate settings.\n"); return 2; }
    const QString configDirectory = parser.isSet("data-dir") ? QDir(parser.value("data-dir")).absolutePath() : QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
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
    const bool published = QFile::exists(QDir(installDirectory).filePath("manifest.txt"));
    int code = 0; bool relaunch = false;
    {
        clipsapp::MainWindow window(configDirectory);
        if (smoke) window.setAttribute(Qt::WA_DontShowOnScreen, true);
        window.show();
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
    // The window is gone, with its capture and its global shortcut: the new version can take over.
    if (relaunch) {
        QStringList arguments{QStringLiteral("--updated-from"), QStringLiteral(CLIPSAPP_VERSION)};
        if (parser.isSet("data-dir")) arguments << QStringLiteral("--data-dir") << configDirectory;
        QProcess::startDetached(QCoreApplication::applicationFilePath(), arguments, installDirectory);
    }
    return code;
}
