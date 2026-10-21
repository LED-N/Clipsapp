#pragma once
#include "core/replay_buffer.h"
#include <QString>
#include <memory>

namespace clipsapp {
// Keeps the bytes of the replay in files of a folder instead of memory. A thread of its own
// writes them, so that a busy disk does not hold the encoder up; Windows keeps the files in its
// cache as long as it can, and removes them when they are closed, even after a crash.
class ReplayDisk {
public:
    // Throws when the folder cannot hold the buffer.
    explicit ReplayDisk(const QString& folder);
    // Waits for the bytes given to be written: a clip being saved may still need them.
    ~ReplayDisk();
    // Gives the packet its bytes on disk. Waits only if the disk is far behind.
    void store(EncodedPacket& packet, QByteArray data);
    // Why the buffer could not be written; empty while all is well.
    QString error() const;
    // What a replay stopped by a power cut may have left in the folder.
    static void sweep(const QString& folder);
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
}
