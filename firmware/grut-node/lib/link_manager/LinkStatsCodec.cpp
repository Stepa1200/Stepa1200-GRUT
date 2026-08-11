#include "LinkStatsCodec.h"

namespace grut {
namespace link {
namespace {

void putU16LE(uint8_t* dst, uint16_t value) {
  dst[0] = static_cast<uint8_t>(value & 0xFFu);
  dst[1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
}

void putU32LE(uint8_t* dst, uint32_t value) {
  dst[0] = static_cast<uint8_t>(value & 0xFFu);
  dst[1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
  dst[2] = static_cast<uint8_t>((value >> 16) & 0xFFu);
  dst[3] = static_cast<uint8_t>((value >> 24) & 0xFFu);
}

uint16_t getU16LE(const uint8_t* src) {
  return static_cast<uint16_t>(src[0]) |
         static_cast<uint16_t>(static_cast<uint16_t>(src[1]) << 8);
}

uint32_t getU32LE(const uint8_t* src) {
  return static_cast<uint32_t>(src[0]) |
         (static_cast<uint32_t>(src[1]) << 8) |
         (static_cast<uint32_t>(src[2]) << 16) |
         (static_cast<uint32_t>(src[3]) << 24);
}

bool isKnownState(uint8_t value) {
  return value <= static_cast<uint8_t>(LinkState::kRecovering);
}

}  // namespace

size_t encodeLinkStats(const LinkStats& stats, uint8_t* outBuffer,
                       size_t outCapacity) {
  if (outBuffer == nullptr || outCapacity < kLinkStatsPayloadSize) {
    return 0;
  }

  outBuffer[0] = kControlSubtypeLinkStats;
  outBuffer[1] = kLinkStatsPayloadVersion;
  outBuffer[2] = static_cast<uint8_t>(stats.state);
  outBuffer[3] = stats.recoveryHeartbeats;
  putU16LE(outBuffer + 4, stats.shortLossPermille);
  putU16LE(outBuffer + 6, stats.longLossPermille);
  putU32LE(outBuffer + 8, stats.heartbeatAgeMs);
  putU32LE(outBuffer + 12, stats.receivedFrames);
  putU32LE(outBuffer + 16, stats.sequenceGaps);
  putU32LE(outBuffer + 20, stats.sendFailures);
  putU32LE(outBuffer + 24, stats.queueDrops);
  return kLinkStatsPayloadSize;
}

bool decodeLinkStats(const uint8_t* payload, size_t payloadLength,
                     LinkStats* outStats) {
  if (payload == nullptr || outStats == nullptr ||
      payloadLength != kLinkStatsPayloadSize ||
      payload[0] != kControlSubtypeLinkStats ||
      payload[1] != kLinkStatsPayloadVersion || !isKnownState(payload[2])) {
    return false;
  }

  LinkStats decoded;
  decoded.state = static_cast<LinkState>(payload[2]);
  decoded.recoveryHeartbeats = payload[3];
  decoded.shortLossPermille = getU16LE(payload + 4);
  decoded.longLossPermille = getU16LE(payload + 6);
  decoded.heartbeatAgeMs = getU32LE(payload + 8);
  decoded.receivedFrames = getU32LE(payload + 12);
  decoded.sequenceGaps = getU32LE(payload + 16);
  decoded.sendFailures = getU32LE(payload + 20);
  decoded.queueDrops = getU32LE(payload + 24);
  *outStats = decoded;
  return true;
}

}  // namespace link
}  // namespace grut
