#pragma once

#include <cstddef>
#include <cstdint>

namespace grut {
namespace discovery {

// Formal kControl payload subtype for neighbor discovery (Stage 4.2,
// ADR 0007's roadmap). The GRUT frame header/CRC/version are
// unchanged; this codec defines only the payload carried inside
// PacketType::kControl, following the exact same subtype-byte pattern
// already established by link::LinkStatsCodec's kControlSubtypeLinkStats.
//
// HELLO deliberately carries no fields beyond the subtype tag itself:
// the sender's identity is already present in every GRUT frame's
// header (GrutFrameHeader::srcAddr), and NeighborTable is keyed on
// exactly that field - duplicating it inside the payload would add
// bytes for no benefit. "Minimum identity information required for
// discovery" is therefore zero additional bytes.
constexpr uint8_t kControlSubtypeHello = 0x02;
constexpr size_t kHelloPayloadSize = 1;

// Serialize a HELLO payload. Returns 0 if outBuffer is null or too
// small, otherwise exactly kHelloPayloadSize.
size_t encodeHello(uint8_t* outBuffer, size_t outCapacity);

// True only if this is a well-formed HELLO payload (correct subtype
// byte, correct length). Does not itself identify the sender - that
// comes from the enclosing GrutFrameHeader::srcAddr, read by the
// caller before/after calling this.
bool decodeHello(const uint8_t* payload, size_t payloadLength);

}  // namespace discovery
}  // namespace grut
