#pragma once
#include <QByteArray>
#include <QList>
#include <deque>
#include <algorithm>

namespace clipsapp {
struct EncodedPacket {
    int stream = 0;
    qint64 ptsUs = 0, dtsUs = 0, durationUs = 0;
    bool keyframe = false;
    QByteArray data;
    qint64 storageBytes() const { return data.size() + sizeof(EncodedPacket) + 64; }
};
struct ReplaySnapshot {
    QList<EncodedPacket> packets;
    qint64 startUs = 0, endUs = 0;
    double seconds() const { return (endUs - startUs) / 1000000.0; }
};
// Caller synchronizes access. Stream zero is video; timestamps share one clock.
class ReplayBuffer {
public:
    ReplayBuffer(qint64 durationUs, qint64 limitBytes);
    void push(EncodedPacket packet);
    ReplaySnapshot snapshot() const;
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
