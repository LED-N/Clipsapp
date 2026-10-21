#pragma once
#include <QByteArray>
#include <QList>
#include <deque>
#include <algorithm>
#include <memory>

namespace clipsapp {
// A file of the replay kept on disk. The packets that point into it keep it: it goes when the
// last of them, in the buffer or in a clip being saved, is gone. A read that fails throws.
class PacketFile {
public:
    virtual ~PacketFile() = default;
    virtual QByteArray read(qint64 offset, int size) const = 0;
};
struct EncodedPacket {
    int stream = 0;
    qint64 ptsUs = 0, dtsUs = 0, durationUs = 0;
    bool keyframe = false;
    // The bytes in memory or, when the replay is kept on disk, where they are in their file.
    QByteArray data;
    std::shared_ptr<const PacketFile> file; qint64 offset = 0; int size = 0;
    qint64 bytes() const { return file ? size : data.size(); }
    qint64 storageBytes() const { return bytes() + sizeof(EncodedPacket) + 64; }
    QByteArray payload() const { return file ? file->read(offset, size) : data; }
};
struct ReplaySnapshot {
    QList<EncodedPacket> packets;
    qint64 startUs = 0, endUs = 0;
    double seconds() const { return (endUs - startUs) / 1000000.0; }
};
// Caller synchronizes access. Stream zero is video; timestamps share one clock. The limit counts
// the bytes of the packets, wherever they are kept.
class ReplayBuffer {
public:
    ReplayBuffer(qint64 durationUs, qint64 limitBytes);
    void push(EncodedPacket packet);
    ReplaySnapshot snapshot() const;
    // What a recording starts with: the packets from the last picture a video can start on, at
    // most one group of pictures before now, in decoding order.
    QList<EncodedPacket> fromLastKeyframe() const;
    void clear();
    qint64 bytes() const { return m_bytes; }
    double seconds() const { return m_firstKey < 0 ? 0.0 : std::max<qint64>(0, m_latestVideo - m_firstKey) / 1000000.0; }
private:
    void discardBefore(qint64 timeUs);
    std::deque<EncodedPacket> m_packets;
    std::deque<qint64> m_keys;
    qint64 m_duration, m_limit, m_bytes = 0, m_latestVideo = -1, m_firstKey = -1;
};
}
