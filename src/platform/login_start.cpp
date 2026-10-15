#include "login_start.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#ifdef Q_OS_WIN
#include <QSettings>
#endif

namespace clipsapp {
namespace {
const QString entryName = QStringLiteral("Clipsapp");
// The program a command starts: the first word, or the first quoted run of words.
QString commandProgram(const QString& command) {
    const auto text = command.trimmed();
    if (text.startsWith('"')) { const int end = text.indexOf('"', 1); return end > 0 ? text.mid(1, end - 1) : QString(); }
    return text.section(' ', 0, 0);
}
#ifndef Q_OS_WIN
QString desktopFile(const QString& location) { return QDir(location).filePath(QStringLiteral("clipsapp.desktop")); }
#endif
}
QString loginStartCommand(const QString& executable) { return QStringLiteral("\"%1\" --autostart").arg(QDir::toNativeSeparators(executable)); }
#ifdef Q_OS_WIN
QString loginStartLocation() { return QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"); }
QString loginStartProgram(const QString& location) { return commandProgram(QSettings(location, QSettings::NativeFormat).value(entryName).toString()); }
bool loginStartWorks(const QString& location) { const auto program = loginStartProgram(location); return !program.isEmpty() && QFile::exists(program); }
bool setLoginStart(const QString& location, const QString& executable, bool present, QString& error) {
    QSettings run(location, QSettings::NativeFormat);
    if (present) run.setValue(entryName, loginStartCommand(executable)); else run.remove(entryName);
    run.sync();
    if (run.status() != QSettings::NoError || run.contains(entryName) != present) { error = present ? QStringLiteral("Le lancement avec Windows n'a pas pu être activé.") : QStringLiteral("Le lancement avec Windows n'a pas pu être retiré."); return false; }
    // The Task Manager turns an entry off beside it, in a key of its own. Asking again here is
    // turning it back on: that mark goes, as it does with the entry.
    if (location == loginStartLocation()) {
        QSettings approved(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run"), QSettings::NativeFormat);
        approved.remove(entryName);
    }
    return true;
}
#else
QString loginStartLocation() { return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)).filePath(QStringLiteral("autostart")); }
QString loginStartProgram(const QString& location) {
    QFile file(desktopFile(location)); if (!file.open(QIODevice::ReadOnly)) return {};
    for (const auto& line : QString::fromUtf8(file.readAll()).split('\n')) if (line.startsWith(QLatin1String("Exec="))) return commandProgram(line.mid(5));
    return {};
}
bool loginStartWorks(const QString& location) { const auto program = loginStartProgram(location); return !program.isEmpty() && QFile::exists(program); }
bool setLoginStart(const QString& location, const QString& executable, bool present, QString& error) {
    const auto path = desktopFile(location);
    if (!present) { if (QFile::exists(path) && !QFile::remove(path)) { error = QStringLiteral("Le lancement à l'ouverture de session n'a pas pu être retiré."); return false; } return true; }
    QSaveFile file(path); const auto text = QStringLiteral("[Desktop Entry]\nType=Application\nName=Clipsapp\nExec=%1\nX-GNOME-Autostart-enabled=true\n").arg(loginStartCommand(executable)).toUtf8();
    if (!QDir().mkpath(location) || !file.open(QIODevice::WriteOnly) || file.write(text) != text.size() || !file.commit()) { error = QStringLiteral("Le lancement à l'ouverture de session n'a pas pu être activé."); return false; }
    return true;
}
#endif
}
