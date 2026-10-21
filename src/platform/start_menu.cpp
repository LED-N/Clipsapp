#include "start_menu.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#ifdef Q_OS_WIN
#include <windows.h>
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>
#endif

namespace clipsapp {
QString startMenuShortcut(const QString& programsDirectory) { return QDir(programsDirectory).filePath(QStringLiteral("Clipsapp.lnk")); }
bool startMenuShortcutWorks(const QString& programsDirectory) {
    const auto target = QFileInfo(startMenuShortcut(programsDirectory)).symLinkTarget();
    return !target.isEmpty() && QFile::exists(target);
}
bool setStartMenuShortcut(const QString& programsDirectory, const QString& executable, bool present, QString& error, const QString& icon) {
    const auto path = startMenuShortcut(programsDirectory);
    // A shortcut is replaced rather than edited: the folder of the application may have moved.
    if (QFileInfo(path).isSymLink() || QFile::exists(path)) {
        if (!QFile::remove(path)) { error = QStringLiteral("Le raccourci du menu Démarrer n'a pas pu être retiré."); return false; }
    }
    if (!present) return true;
    if (programsDirectory.isEmpty() || !QDir().mkpath(programsDirectory) || !QFile::link(QDir::toNativeSeparators(executable), path)) {
        error = QStringLiteral("Le raccourci du menu Démarrer n'a pas pu être créé."); return false;
    }
    if (!icon.isEmpty()) setShortcutIcon(path, icon);
    return true;
}
void setShortcutIcon(const QString& shortcut, const QString& icon) {
#ifdef Q_OS_WIN
    if (!QFileInfo(shortcut).isSymLink()) return;
    const auto native = QDir::toNativeSeparators(shortcut).toStdWString(); const auto picture = QDir::toNativeSeparators(icon).toStdWString();
    IShellLinkW* link = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void**>(&link)))) return;
    IPersistFile* file = nullptr;
    if (SUCCEEDED(link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&file)))) {
        // No icon given: the program's own, at index 0 of its executable.
        if (SUCCEEDED(file->Load(native.c_str(), STGM_READWRITE))) {
            wchar_t target[MAX_PATH] = {}; link->GetPath(target, MAX_PATH, nullptr, 0);
            link->SetIconLocation(icon.isEmpty() ? target : picture.c_str(), 0); file->Save(native.c_str(), TRUE);
        }
        file->Release();
    }
    link->Release();
    // The Start menu shows the new icon without waiting for its cache.
    SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATHW, native.c_str(), nullptr);
#else
    Q_UNUSED(shortcut); Q_UNUSED(icon);
#endif
}
}
