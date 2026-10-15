#include "uninstall.h"
#include "login_start.h"
#include "start_menu.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

namespace clipsapp {
namespace {
QString identity(const QString& path) {
    auto key = QDir::cleanPath(QDir::fromNativeSeparators(path));
#ifdef Q_OS_WIN
    key = key.toCaseFolded();
#endif
    return key;
}
bool inside(const QString& path, const QString& directory) { const auto child = identity(path), parent = identity(directory); return child == parent || child.startsWith(parent + '/'); }
// Read as it is written, without the checks of the settings: even a file the application no
// longer accepts says where the clips are.
QString libraryOf(const QString& configDirectory) {
    QFile file(QDir(configDirectory).filePath(QStringLiteral("settings.json")));
    if (!file.open(QIODevice::ReadOnly) || file.size() > 4 * 1024 * 1024) return {};
    return QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("libraryPath")).toString();
}
}
QString clipsInside(const UninstallPlan& plan) { const auto library = libraryOf(plan.configDirectory); return !library.isEmpty() && inside(library, plan.installDirectory) ? library : QString(); }
UninstallResult removeTraces(const UninstallPlan& plan) {
    UninstallResult result;
    if (const auto library = clipsInside(plan); !library.isEmpty()) {
        result.code = UninstallClipsInside; result.problems << QStringLiteral("Les clips sont rangés dans le dossier de Clipsapp : %1").arg(QDir::toNativeSeparators(library)); return result;
    }
    const auto ours = [&plan](const QString& program) { return !program.isEmpty() && identity(program) == identity(plan.executable); };
    QString error;
    if (!plan.programsDirectory.isEmpty() && ours(QFileInfo(startMenuShortcut(plan.programsDirectory)).symLinkTarget()) && !setStartMenuShortcut(plan.programsDirectory, plan.executable, false, error)) result.problems << error;
    if (!plan.loginLocation.isEmpty() && ours(loginStartProgram(plan.loginLocation)) && !setLoginStart(plan.loginLocation, plan.executable, false, error)) result.problems << error;
    if (!plan.keepSettings && !plan.configDirectory.isEmpty() && QFileInfo(plan.configDirectory).isDir() && !QDir(plan.configDirectory).removeRecursively())
        result.problems << QStringLiteral("Les réglages n'ont pas pu être tous supprimés : %1").arg(QDir::toNativeSeparators(plan.configDirectory));
    if (!result.problems.isEmpty()) result.code = UninstallIncomplete;
    return result;
}
}
