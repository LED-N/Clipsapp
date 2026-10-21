#include "replay_buffer.h"
#include <algorithm>
#include <utility>

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
QList<EncodedPacket> ReplayBuffer::fromLastKeyframe() const {
    if (m_keys.empty()) return {};
    const qint64 key = m_keys.back(); QList<EncodedPacket> result;
    for (const auto& packet : m_packets) if (packet.stream == 0 ? packet.dtsUs >= key : packet.ptsUs >= key) result.append(packet);
    // At the same instant, the picture comes first: the recording starts on it.
    std::stable_sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return std::make_pair(a.dtsUs, a.stream != 0) < std::make_pair(b.dtsUs, b.stream != 0); });
    return result;
}
ReplaySnapshot ReplayBuffer::snapshot() const {
    ReplaySnapshot result;
    const EncodedPacket* key = nullptr;
    for (const auto& packet : m_packets) if (packet.stream == 0 && packet.keyframe && (!key || packet.dtsUs < key->dtsUs)) key = &packet;
    if (!key) return result;
    const auto decodingOrder = [](const auto& a, const auto& b) { return a.dtsUs < b.dtsUs; };
    QList<EncodedPacket> video;
    for (const auto& packet : m_packets) if (packet.stream == 0 && packet.dtsUs >= key->dtsUs) video.append(packet);
    std::stable_sort(video.begin(), video.end(), decodingOrder);
    // A B-frame is decoded after the picture that follows it on screen. Such a stream may only
    // stop where the next picture decoded is also the next one shown: cut anywhere else, the clip
    // would end on a hole. The few pictures past that point are still being reordered.
    qsizetype count = video.size();
    if (std::any_of(video.cbegin(), video.cend(), [](const auto& packet) { return packet.ptsUs != packet.dtsUs; })) {
        count = 0; qint64 latest = video.first().ptsUs;
        for (qsizetype index = 0; index + 1 < video.size(); ++index) {
            latest = std::max(latest, video[index].ptsUs);
            if (video[index + 1].ptsUs > latest) count = index + 1;
        }
    }
    if (!count) return result;
    // Time starts where the first picture is shown, which reordering puts after where it is decoded.
    result.startUs = key->ptsUs; result.packets = video.first(count);
    for (const auto& packet : result.packets) result.endUs = std::max(result.endUs, packet.ptsUs + packet.durationUs);
    for (const auto& packet : m_packets)
        if (packet.stream != 0 && packet.ptsUs >= result.startUs && packet.ptsUs < result.endUs) result.packets.append(packet);
    std::stable_sort(result.packets.begin(), result.packets.end(), decodingOrder);
    return result;
}
}
