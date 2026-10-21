#include "folder_watch.h"
#include <QDir>
#ifdef Q_OS_WIN
#include <windows.h>
#include <atomic>
#include <thread>
#include <vector>
#else
#include <QFileSystemWatcher>
#endif

namespace clipsapp {
#ifdef Q_OS_WIN
// One request on the folder itself covers everything under it. The folder is opened so that
// others may still rename and delete what it holds, and the folder too.
struct FolderWatcher::Worker {
    HANDLE directory = INVALID_HANDLE_VALUE, stop = CreateEventW(nullptr, TRUE, FALSE, nullptr), signal = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::thread thread; std::atomic_bool alive{false};
    std::vector<DWORD> buffer = std::vector<DWORD>(16384); OVERLAPPED overlapped{};
    ~Worker() {
        if (stop) SetEvent(stop);
        if (thread.joinable()) thread.join();
        if (directory != INVALID_HANDLE_VALUE) CloseHandle(directory);
        if (stop) CloseHandle(stop);
        if (signal) CloseHandle(signal);
    }
    // Windows keeps what changes only once it has been asked: the first request is made before
    // watch() returns, so that nothing done just after is missed.
    bool arm() {
        constexpr DWORD filter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE;
        ResetEvent(signal); overlapped = {}; overlapped.hEvent = signal;
        return ReadDirectoryChangesW(directory, buffer.data(), DWORD(buffer.size() * sizeof(DWORD)), TRUE, filter, nullptr, &overlapped, nullptr);
    }
    void run(FolderWatcher* owner) {
        const auto notify = [owner](QStringList paths) { QMetaObject::invokeMethod(owner, [owner, paths] { emit owner->changed(paths); }, Qt::QueuedConnection); };
        for (bool armed = true;; armed = arm()) {
            if (!armed) { notify({}); break; }
            const HANDLE handles[]{signal, stop};
            if (WaitForMultipleObjects(2, handles, FALSE, INFINITE) != WAIT_OBJECT_0) {
                CancelIoEx(directory, &overlapped); DWORD ignored = 0; GetOverlappedResult(directory, &overlapped, &ignored, TRUE); break;
            }
            DWORD bytes = 0;
            // The folder itself went away, or can no longer be read.
            if (!GetOverlappedResult(directory, &overlapped, &bytes, FALSE)) { notify({}); break; }
            // More happened than the buffer could tell.
            if (!bytes) { notify({}); continue; }
            QStringList paths; auto* entry = reinterpret_cast<const BYTE*>(buffer.data());
            for (;;) {
                const auto* info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(entry);
                const auto path = QDir::fromNativeSeparators(QString::fromWCharArray(info->FileName, int(info->FileNameLength / sizeof(wchar_t))));
                if (!paths.contains(path)) paths.append(path);
                if (!info->NextEntryOffset) break;
                entry += info->NextEntryOffset;
            }
            notify(paths);
        }
        // Closed here rather than by the owner: a folder deleted meanwhile is released at once.
        CloseHandle(directory); directory = INVALID_HANDLE_VALUE; alive = false;
    }
};
FolderWatcher::FolderWatcher(QObject* parent) : QObject(parent) {}
FolderWatcher::~FolderWatcher() = default;
bool FolderWatcher::watching() const { return m_worker && m_worker->alive; }
void FolderWatcher::watch(const QString& directory) {
    if (directory == m_directory && (directory.isEmpty() || watching())) return;
    m_worker.reset(); m_directory = directory;
    if (directory.isEmpty()) return;
    auto worker = std::make_unique<Worker>(); if (!worker->stop || !worker->signal) return;
    worker->directory = CreateFileW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(directory).utf16()), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
    if (worker->directory == INVALID_HANDLE_VALUE || !worker->arm()) return;
    worker->alive = true; auto* state = worker.get(); worker->thread = std::thread([state, this] { state->run(this); });
    m_worker = std::move(worker);
}
#else
// inotify does not hold folders: the folder and its sub-folders are each followed.
struct FolderWatcher::Worker { QFileSystemWatcher watcher; };
FolderWatcher::FolderWatcher(QObject* parent) : QObject(parent) {}
FolderWatcher::~FolderWatcher() = default;
bool FolderWatcher::watching() const { return m_worker && !m_worker->watcher.directories().isEmpty(); }
void FolderWatcher::watch(const QString& directory) {
    if (directory == m_directory && (directory.isEmpty() || watching())) return;
    m_worker.reset(); m_directory = directory;
    if (directory.isEmpty() || !QDir(directory).exists()) return;
    m_worker = std::make_unique<Worker>(); auto* watcher = &m_worker->watcher;
    const auto follow = [watcher, directory] {
        QStringList folders{directory}; for (int index = 0; index < folders.size() && folders.size() < 1000; ++index)
            for (const auto& child : QDir(folders[index]).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks)) folders.append(child.absoluteFilePath());
        const auto current = watcher->directories(); for (const auto& folder : folders) if (!current.contains(folder)) watcher->addPath(folder);
    };
    follow();
    connect(watcher, &QFileSystemWatcher::directoryChanged, this, [this, follow, directory](const QString& path) { follow(); emit changed({QDir(directory).relativeFilePath(path)}); });
}
#endif
}
