#pragma once

#include <cstddef>
#include <cstdint>

namespace grut {
namespace protocol {

// GRUT wire protocol foundation (ADR 0005). Pure framing/encoding logic
// only - no ESP-NOW, no UART, no Wi-Fi, no roles, no retransmission.
// See docs/PROTOCOL.md for the full human-readable spec; this header is
// the source of truth for exact field widths and constants.

constexpr uint8_t kProtocolVersion = 1;

// Reserved node address.
constexpr uint8_t kBroadcastAddress = 0xFF;

enum class PacketType : uint8_t {
  kData = 0x01,       // opaque UART byte-stream chunk
  kHeartbeat = 0x02,  // LinkManager heartbeat supervision
  kControl = 0x03,    // management payloads (currently LINK_STATS v1)
};

// Bitmask flags. Most bits are reserved for future milestones (ack,
// fragmentation) - only their bit positions are fixed now, so the wire
// format does not need to change when that behavior is added later. No
// flag has any behavioral effect in this milestone's code.
enum GrutFlag : uint8_t {
  kFlagNone = 0x00,
  kFlagAckRequested = 0x01,  // reserved - not implemented yet
  kFlagFragment = 0x02,      // reserved - general fragmentation is out
                              // of scope for v0.2.0 (ADR 0005)
  kFlagBroadcast = 0x04,     // informational: dstAddr is kBroadcastAddress
};

// In-memory representation of a GRUT frame header. Field widths here
// match the wire format exactly (see docs/PROTOCOL.md). FrameCodec is
// responsible for serializing/deserializing these in a fixed
// little-endian byte order on the wire, independent of host struct
// padding or endianness - do not cast this struct directly onto a wire
// buffer.
struct GrutFrameHeader {
  uint8_t version = kProtocolVersion;
  uint8_t type = static_cast<uint8_t>(PacketType::kData);
  uint8_t flags = kFlagNone;
  uint8_t srcAddr = 0;
  uint8_t dstAddr = 0;
  uint8_t ttl = 0;
  uint16_t sequence = 0;
  uint8_t payloadLength = 0;  // set by encodeFrame(); informational on decode
};

// Wire sizes. Deliberately NOT sizeof(GrutFrameHeader) - the wire format
// is defined independently of host struct layout/padding.
constexpr size_t kHeaderWireSize = 9;  // version+type+flags+src+dst+ttl+seq(2)+len
constexpr size_t kCrcWireSize = 2;
constexpr size_t kFrameOverheadBytes = kHeaderWireSize + kCrcWireSize;

// ESP8285 uses the ESP-NOW v1.0 packet-size model (ADR 0005): a single
// application payload (the entire GRUT frame) must not exceed this many
// bytes.
constexpr size_t kEspNowV1MaxPayloadBytes = 250;

constexpr size_t kMaxGrutPayloadBytes =
    kEspNowV1MaxPayloadBytes - kFrameOverheadBytes;

constexpr size_t kMaxFrameSizeBytes = kEspNowV1MaxPayloadBytes;

static_assert(kFrameOverheadBytes + kMaxGrutPayloadBytes <=
                  kEspNowV1MaxPayloadBytes,
              "GRUT frame exceeds ESP-NOW v1 payload limit");

}  // namespace protocol
}  // namespace grut
