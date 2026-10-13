#include "replay_buffer.h"
#include <algorithm>

namespace clipsapp {
ReplayBuffer::ReplayBuffer(qint64 durationUs, qint64 limitBytes)
    : m_duration(durationUs), m_limit(limitBytes) {}
void ReplayBuffer::clear() { m_packets.clear(); m_keys.clear(); m_bytes = 0; m_latestVideo = -1; m_firstKey = -1; }
void ReplayBuffer::discardBefore(qint64 timeUs) {
    m_firstKey = timeUs;
    // Audio and video packets may arrive in a different order.
    for (auto it = m_packets.begin(); it != m_packets.end();) {
        if (it->dtsUs < timeUs) { m_bytes -= it->storageBytes(); it = m_packets.erase(it); }
        else ++it;
    }
}
void ReplayBuffer::push(EncodedPacket packet) {
    if (m_firstKey >= 0 && packet.dtsUs < m_firstKey) return;
    const bool newKey = packet.stream == 0 && packet.keyframe;
    if (newKey && m_firstKey < 0) m_firstKey = packet.dtsUs;
    if (newKey) m_keys.push_back(packet.dtsUs);
    if (packet.stream == 0) m_latestVideo = std::max(m_latestVideo, packet.ptsUs + packet.durationUs);
    m_bytes += packet.storageBytes(); m_packets.push_back(std::move(packet));
    // Keep the keyframe just before the duration boundary (at most one extra GOP).
    while (m_keys.size() >= 2 && m_keys[1] <= m_latestVideo - m_duration) {
        m_keys.pop_front(); discardBefore(m_keys.front());
    }
    while (m_bytes > m_limit && !m_packets.empty()) {
        if (m_keys.size() < 2) { clear(); break; } // One GOP exceeds the cap; wait for the next keyframe.
        m_keys.pop_front(); discardBefore(m_keys.front());
    }
}
ReplaySnapshot ReplayBuffer::snapshot() const {
    ReplaySnapshot result;
    qint64 start = -1;
    for (const auto& packet : m_packets) if (packet.stream == 0 && packet.keyframe)
        if (start < 0 || packet.dtsUs < start) start = packet.dtsUs;
    if (start < 0) return result;
    result.startUs = start; result.endUs = m_latestVideo;
    for (const auto& packet : m_packets)
        if (packet.dtsUs >= start && packet.ptsUs < result.endUs) result.packets.append(packet);
    std::stable_sort(result.packets.begin(), result.packets.end(), [](const auto& a, const auto& b) { return a.dtsUs < b.dtsUs; });
    return result;
}
}
