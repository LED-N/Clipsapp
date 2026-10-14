#include "start_menu.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace clipsapp {
QString startMenuShortcut(const QString& programsDirectory) { return QDir(programsDirectory).filePath(QStringLiteral("Clipsapp.lnk")); }
bool startMenuShortcutWorks(const QString& programsDirectory) {
    const auto target = QFileInfo(startMenuShortcut(programsDirectory)).symLinkTarget();
    return !target.isEmpty() && QFile::exists(target);
}
bool setStartMenuShortcut(const QString& programsDirectory, const QString& executable, bool present, QString& error) {
    const auto path = startMenuShortcut(programsDirectory);
    // A shortcut is replaced rather than edited: the folder of the application may have moved.
    if (QFileInfo(path).isSymLink() || QFile::exists(path)) {
        if (!QFile::remove(path)) { error = QStringLiteral("Le raccourci du menu Démarrer n'a pas pu être retiré."); return false; }
    }
    if (!present) return true;
    if (programsDirectory.isEmpty() || !QDir().mkpath(programsDirectory) || !QFile::link(QDir::toNativeSeparators(executable), path)) {
        error = QStringLiteral("Le raccourci du menu Démarrer n'a pas pu être créé."); return false;
    }
    return true;
}
}
