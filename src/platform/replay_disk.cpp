#include "replay_disk.h"
#include <QCoreApplication>
#include <QDir>
#include <windows.h>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace clipsapp {
namespace {
// Files of 64 MiB: the oldest goes as soon as the replay no longer needs any of it.
constexpr qint64 segmentBytes = 64LL * 1048576;
// Past this much waiting to be written, the encoder waits for the disk.
constexpr qint64 pendingLimit = 256LL * 1048576;
QString systemMessage(DWORD code) {
    wchar_t* text = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0, reinterpret_cast<LPWSTR>(&text), 0, nullptr);
    const auto result = text ? QString::fromWCharArray(text).trimmed() : QStringLiteral("erreur Windows %1").arg(code); LocalFree(text); return result;
}
// What the writer and the readers share; it outlives the store while a clip is being saved.
struct Shared { std::mutex mutex; std::condition_variable changed; QString error; };
class Segment final : public PacketFile {
public:
    Segment(HANDLE handle, std::shared_ptr<Shared> shared) : m_handle(handle), m_shared(std::move(shared)) {}
    // Closing the file removes it.
    ~Segment() override { CloseHandle(m_handle); }
    QByteArray read(qint64 offset, int size) const override {
        {
            std::unique_lock lock(m_shared->mutex);
            m_shared->changed.wait(lock, [&] { return written >= offset + size || !m_shared->error.isEmpty(); });
            if (written < offset + size) throw std::runtime_error(("Buffer sur disque illisible : " + m_shared->error).toStdString());
        }
        QByteArray data(size, Qt::Uninitialized); OVERLAPPED at{}; at.Offset = DWORD(offset); at.OffsetHigh = DWORD(offset >> 32); DWORD read = 0;
        if (!ReadFile(m_handle, data.data(), DWORD(size), &read, &at) || read != DWORD(size)) throw std::runtime_error(("Lecture du buffer sur disque : " + systemMessage(GetLastError())).toStdString());
        return data;
    }
    bool write(qint64 offset, const QByteArray& data) const {
        OVERLAPPED at{}; at.Offset = DWORD(offset); at.OffsetHigh = DWORD(offset >> 32); DWORD done = 0;
        return WriteFile(m_handle, data.constData(), DWORD(data.size()), &done, &at) && done == DWORD(data.size());
    }
    // Guarded by the shared mutex: how far the file is written.
    qint64 written = 0;
private:
    HANDLE m_handle;
    std::shared_ptr<Shared> m_shared;
};
QString prefix() { return QStringLiteral("clipsapp-replay-%1-").arg(QCoreApplication::applicationPid()); }
}
struct ReplayDisk::Impl {
    struct Job { std::shared_ptr<Segment> segment; qint64 offset = 0; QByteArray data; };
    QString folder;
    std::shared_ptr<Shared> shared = std::make_shared<Shared>();
    std::shared_ptr<Segment> current; qint64 used = 0; int sequence = 0;
    std::deque<Job> jobs; qint64 pending = 0; bool stop = false;
    std::thread writer;
    void open() {
        const auto path = QDir::toNativeSeparators(QDir(folder).filePath(prefix() + QString::number(++sequence) + QStringLiteral(".tmp")));
        // Temporary: Windows writes it out only when it needs the memory. Deleted on close, even when the process dies.
        const HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
        if (handle == INVALID_HANDLE_VALUE) throw std::runtime_error(QStringLiteral("Impossible d'écrire le buffer dans « %1 » : %2").arg(QDir::toNativeSeparators(folder), systemMessage(GetLastError())).toStdString());
        current = std::make_shared<Segment>(handle, shared); used = 0;
    }
    void run() {
        for (;;) {
            std::deque<Job> batch; bool broken;
            {
                std::unique_lock lock(shared->mutex);
                shared->changed.wait(lock, [&] { return stop || !jobs.empty(); });
                if (jobs.empty()) return;
                batch.swap(jobs); broken = !shared->error.isEmpty();
            }
            // Packets that follow each other in one file go in one write of a few MiB.
            for (size_t first = 0; first < batch.size();) {
                size_t last = first + 1; qint64 size = batch[first].data.size();
                while (last < batch.size() && batch[last].segment == batch[first].segment && batch[last].offset == batch[first].offset + size && size < 8 * 1048576) size += batch[last++].data.size();
                QByteArray block; if (last - first == 1) block = batch[first].data; else { block.reserve(size); for (size_t index = first; index < last; ++index) block.append(batch[index].data); }
                // Once a write failed, what follows is dropped: the replay stops on that error.
                const bool failed = broken || !batch[first].segment->write(batch[first].offset, block);
                const DWORD code = GetLastError(); broken |= failed;
                {
                    std::lock_guard lock(shared->mutex);
                    if (failed && shared->error.isEmpty()) shared->error = systemMessage(code);
                    if (!failed) batch[first].segment->written = batch[first].offset + size;
                    pending -= size;
                }
                shared->changed.notify_all(); first = last;
            }
        }
    }
};
ReplayDisk::ReplayDisk(const QString& folder) : m_impl(std::make_unique<Impl>()) {
    m_impl->folder = folder;
    if (!QDir().mkpath(folder)) throw std::runtime_error(QStringLiteral("Impossible de créer le dossier du buffer « %1 ».").arg(QDir::toNativeSeparators(folder)).toStdString());
    sweep(folder); m_impl->open();
    m_impl->writer = std::thread([impl = m_impl.get()] { impl->run(); });
}
ReplayDisk::~ReplayDisk() {
    { std::lock_guard lock(m_impl->shared->mutex); m_impl->stop = true; }
    m_impl->shared->changed.notify_all(); m_impl->writer.join();
}
void ReplayDisk::store(EncodedPacket& packet, QByteArray data) {
    auto& impl = *m_impl;
    if (impl.used > 0 && impl.used + data.size() > segmentBytes) impl.open();
    packet.file = impl.current; packet.offset = impl.used; packet.size = int(data.size()); impl.used += data.size();
    std::unique_lock lock(impl.shared->mutex);
    impl.shared->changed.wait(lock, [&] { return impl.pending < pendingLimit || !impl.shared->error.isEmpty(); });
    impl.pending += data.size(); impl.jobs.push_back({impl.current, packet.offset, std::move(data)});
    lock.unlock(); impl.shared->changed.notify_all();
}
QString ReplayDisk::error() const { std::lock_guard lock(m_impl->shared->mutex); return m_impl->shared->error; }
void ReplayDisk::sweep(const QString& folder) {
    // A file still open by another Clipsapp cannot be removed, and stays.
    QDir directory(folder);
    for (const auto& name : directory.entryList({QStringLiteral("clipsapp-replay-*.tmp")}, QDir::Files)) directory.remove(name);
}
}
