#pragma once

#include <cstddef>
#include <cstdint>

#include "LinkManager.h"

namespace grut {
namespace link {

// Formal kControl payload subtype for LinkManager v1 statistics (ADR 0007).
// The GRUT frame header/CRC/version are unchanged; this codec defines only
// the payload carried inside PacketType::kControl.
constexpr uint8_t kControlSubtypeLinkStats = 0x01;
constexpr uint8_t kLinkStatsPayloadVersion = 1;
constexpr size_t kLinkStatsPayloadSize = 28;

// Serialize one LinkStats snapshot into the fixed v1 LINK_STATS payload.
// Returns 0 if outBuffer is null or too small, otherwise exactly
// kLinkStatsPayloadSize.
size_t encodeLinkStats(const LinkStats& stats, uint8_t* outBuffer,
                       size_t outCapacity);

// Decode exactly one v1 LINK_STATS payload. Returns false for the wrong
// subtype/version/length or a null output pointer.
bool decodeLinkStats(const uint8_t* payload, size_t payloadLength,
                     LinkStats* outStats);

}  // namespace link
}  // namespace grut
