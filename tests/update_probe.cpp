// Manual probe: asks GitHub for the latest release of a repository, exactly as the application
// does, and says what it would conclude. Nothing is downloaded or installed.
#include "ui/update_service.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QSslSocket>
#include <QTimer>
#include <cstdio>

using namespace clipsapp;
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCommandLineParser parser; parser.addHelpOption();
    parser.addOption({"repository", "GitHub repository to ask, as owner/name", "owner/name"});
    parser.addOption({"current", "Version to compare the latest release with", "version", "0.0.0"}); parser.process(app);
    if (!parser.isSet("repository")) return 2;
    UpdateService service(parser.value("current"), QCoreApplication::applicationDirPath()); service.setRepository(parser.value("repository"));
    if (!service.enabled()) { std::printf("Repository must be written owner/name.\n"); return 2; }
    std::printf("TLS backend: %s\n", qPrintable(QSslSocket::activeBackend()));
    QObject::connect(&service, &UpdateService::available, &app, [&] {
        const auto& info = service.info(); std::printf("Update available: %s\n  archive: %s (%lld bytes)\n  sha256: %s\n  from: %s\n", qPrintable(info.version.toString()), qPrintable(info.fileName), static_cast<long long>(info.bytes), info.sha256.isEmpty() ? "not announced" : info.sha256.constData(), qPrintable(info.download.toString()));
        app.exit(0);
    });
    QObject::connect(&service, &UpdateService::upToDate, &app, [&] { std::printf("No release newer than %s.\n", qPrintable(service.currentVersion())); app.exit(0); });
    QObject::connect(&service, &UpdateService::checkFailed, &app, [&](const QString& error) { std::printf("GitHub answered, or failed to, with: %s\n", qPrintable(error)); app.exit(1); });
    QTimer::singleShot(0, &service, [&] { service.check(true); });
    QTimer::singleShot(30000, &app, [&] { std::printf("No answer after 30 s.\n"); app.exit(1); });
    return app.exec();
}
