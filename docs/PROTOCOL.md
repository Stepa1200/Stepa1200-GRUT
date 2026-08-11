# GRUT Wire Protocol

## Status

Foundation implemented (ADR 0005, v0.2.0 "UART over ESP-NOW Bridge",
Milestone 1). This document is the human-readable companion to
`firmware/grut-node/lib/grut_protocol/` - if they ever disagree, the
code is the source of truth for exact field widths, and this document
should be corrected to match it.

Implemented so far: frame header, packet types, flags, CRC, encoder,
decoder, ESP-NOW v1 size limit, ESP-NOW driver, UART chunking
(FrameBuilder/FrameReceiver), heartbeat bring-up frames, and node-wide
sequence allocation. **Not yet implemented:** retransmission,
LinkManager, mesh/routing. See ADR 0005 for the full roadmap.

## Design principle

GRUT defines its own wire protocol, independent of any specific
transport. ESP-NOW is the first transport that will carry GRUT frames,
but the frame format itself has no ESP-NOW-specific concepts baked in -
the same framing, addressing, and CRC logic is intended to run
unchanged over UDP, Ethernet, or LoRa in the future, with only the
driver layer (`ESP-NOW Driver` in the pipeline) changing.

## Frame layout

```
+------------------+------------------------+----------------+
|  GrutFrameHeader  |   payload (0..239 B)   |  CRC16 (2 B)   |
|     (9 bytes)     |                        |  little-endian |
+------------------+------------------------+----------------+
```

The CRC covers every byte of the header and payload, in that order (not
itself). Total frame size (header + payload + CRC) must never exceed
250 bytes - the ESP-NOW v1.0 application payload limit for ESP8285.

## Header fields (9 bytes on the wire, little-endian)

| Offset | Field           | Type    | Size | Meaning |
|--------|-----------------|---------|------|---------|
| 0      | `version`       | uint8   | 1    | Protocol version. Currently `1`. Frames with any other value are rejected by the decoder. |
| 1      | `type`          | uint8   | 1    | Packet type - see [Packet types](#packet-types). |
| 2      | `flags`         | uint8   | 1    | Bitmask - see [Flags](#flags). |
| 3      | `srcAddr`       | uint8   | 1    | Sender's node address. |
| 4      | `dstAddr`       | uint8   | 1    | Destination node address, or `0xFF` (broadcast). |
| 5      | `ttl`           | uint8   | 1    | Time-to-live. Not acted upon yet (no relay/mesh in this milestone) - carried for forward compatibility. |
| 6-7    | `sequence`      | uint16  | 2    | Per-sender monotonically increasing counter, little-endian on the wire. One node-wide allocator increments it for every outbound GRUT frame regardless of packet type; it wraps `65535 -> 0`. The codec only serializes/deserializes the supplied value. |
| 8      | `payloadLength` | uint8   | 1    | Number of payload bytes following the header. Set automatically by the encoder; used by the decoder to locate the CRC trailer. |

The in-memory `GrutFrameHeader` C++ struct (see `GrutProtocol.h`) uses
the same field names and logical widths, but its `sizeof()` is **not**
the wire size (compiler padding may differ - currently 10 bytes in
memory vs. 9 on the wire). The header is always serialized/deserialized
field-by-field by `FrameCodec`, never memcpy'd as a raw struct.

## Packet types

| Value | Name        | Meaning |
|-------|-------------|---------|
| `0x01`| `kData`     | An opaque chunk of the UART byte stream. No relationship to MAVLink message boundaries - see [UART as an opaque byte stream](#uart-as-an-opaque-byte-stream). |
| `0x02`| `kHeartbeat`| Empty heartbeat frame used by LinkManager supervision. |
| `0x03`| `kControl`  | Control/management frame. LINK_STATS v1 is the first formal payload subtype. |

## Flags

`flags` is a bitmask. Bit positions are fixed now so the wire format
does not need to change when the corresponding behavior is implemented
later. **No flag has any behavioral effect in this milestone** - only
`kFlagBroadcast` is currently informational (set when `dstAddr` is the
broadcast address), and even that is not enforced by the codec.

| Bit  | Name               | Meaning |
|------|--------------------|---------|
| 0x01 | `kFlagAckRequested`| Reserved. Acknowledgement/retransmission is out of scope for v0.2.0 (ADR 0005). |
| 0x02 | `kFlagFragment`    | Reserved. General-purpose fragmentation/reassembly of logical messages larger than one frame is out of scope for v0.2.0 (ADR 0005). |
| 0x04 | `kFlagBroadcast`   | Informational: frame's `dstAddr` is `kBroadcastAddress`. |

## Addressing

Addresses are a single byte (`0`-`254`); `0xFF` (`kBroadcastAddress`) is
reserved and means "all nodes". There is no automatic address
assignment in this milestone - address values are assigned externally
(manually, for now) to each node.

A 1-byte address space (255 usable addresses) is intentionally minimal
for the current two-node bridge and the near-term mesh roadmap (a small
number of AIR/GROUND/RELAY nodes). If the network ever needs more than
254 addressable nodes, this is a protocol version bump (widening this
field breaks wire compatibility with older firmware), not a silent
change.

## CRC

CRC-16/CCITT-FALSE: polynomial `0x1021`, initial value `0xFFFF`, no
input/output reflection, no final XOR. Computed over the entire
serialized header + payload (everything except the 2-byte CRC field
itself), stored little-endian as the frame's last 2 bytes.

Known test vector (used in the automated tests): the CRC of the ASCII
bytes `"123456789"` is `0x29B1`.

A single-bit or single-byte corruption anywhere in the header or
payload is detected by the CRC check and the frame is rejected
(`DecodeResult::kCrcMismatch`); the codec does not attempt any error
correction.

## TTL

Present in the header, decremented by nobody yet. Reserved for the
future relay/mesh milestone, where a `RELAY` node would decrement `ttl`
and drop the frame if it reaches zero rather than forward it
indefinitely.

## UART as an opaque byte stream

Per ADR 0005, `kData` frames do not need to align with MAVLink message
boundaries - FrameBuilder treats UART input as a raw byte stream and
chunks it into bounded `kData` frames (currently up to 200 payload
bytes, comfortably under the 239-byte maximum). FrameReceiver writes
DATA payload bytes back to UART in receive order. A single MAVLink
message may therefore span two GRUT
frames; if a frame is lost, the reassembled UART stream will contain a
gap that the downstream MAVLink parser (e.g. Mission Planner) must
tolerate on its own. GRUT v0.2.0 does not guarantee lossless delivery -
see ADR 0005 for the full list of what is deliberately out of scope.

## Heartbeat and control messages

`PacketType::kHeartbeat` is transmitted as an empty frame at the bridge
heartbeat interval. `PacketType::kControl` carries management payloads.
Both packet types consume the same node-wide `sequence` counter as `kData`;
this is required so a receiver can use one authoritative GRUT-frame
sequence for link-loss accounting.

### LINK_STATS control payload v1

The first formal `kControl` subtype is `LINK_STATS` (`subtype = 0x01`).
It exports one LinkManager snapshot every 3 seconds. Control frames are
consumed internally and are never forwarded to the opaque UART byte stream.
All multi-byte fields are little-endian.

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | control subtype = `0x01` (`LINK_STATS`) |
| 1 | 1 | payload version = `1` |
| 2 | 1 | LinkState (`UNKNOWN=0`, `UP=1`, `DEGRADED=2`, `DOWN=3`, `RECOVERING=4`) |
| 3 | 1 | recovery heartbeat count |
| 4 | 2 | short-window loss, permille |
| 6 | 2 | long-window loss, permille |
| 8 | 4 | heartbeat age, ms (`0xFFFFFFFF` = never seen) |
| 12 | 4 | valid GRUT frames received |
| 16 | 4 | GRUT sequence gaps |
| 20 | 4 | send failures |
| 24 | 4 | queue drops |

Total payload size: **28 bytes**; serialized GRUT frame size: **39 bytes**.
Unknown control subtypes or payload versions are ignored.

Compatibility: this formalizes the previously ad-hoc `kControl` statistics
payload without changing the GRUT frame header, CRC, or protocol version.
An older peer will ignore the new control payload, but AIR and GROUND should
run matching firmware so both sides expose the same LinkManager semantics.

### Sequence compatibility note

Before LinkManager work, bridge firmware assigned advancing sequence
numbers only to `kData`; temporary heartbeat/control frames therefore
usually carried zero. From the node-wide sequence change onward, every
outbound GRUT frame consumes one sequence value. The wire layout and
protocol version remain unchanged, but AIR and GROUND should be updated
as a pair: mixing old and new sequence semantics makes loss counters
ambiguous even though UART payload transport itself remains decodable.

## Size limits

- ESP-NOW v1.0 application payload limit (ESP8285): **250 bytes**,
  enforced by hardware/SDK, not by GRUT.
- Frame overhead (header + CRC trailer): **11 bytes**.
- Maximum GRUT payload: **239 bytes** (`250 - 11`).
- These constants are defined once, in `GrutProtocol.h`, and checked at
  compile time:

  ```cpp
  static_assert(kFrameOverheadBytes + kMaxGrutPayloadBytes <=
                    kEspNowV1MaxPayloadBytes,
                "GRUT frame exceeds ESP-NOW v1 payload limit");
  ```

## Explicit non-goals for v0.2.0 (see ADR 0005)

- No fragmentation/reassembly of logical messages larger than one
  frame.
- No retransmission or acknowledgement.
- No automatic role or address detection.
- No routing, relay, or mesh.
- No encryption or authentication.

## Source of truth

`firmware/grut-node/lib/grut_protocol/`:
- `GrutProtocol.h` - constants, packet types, flags, header struct.
- `Crc16.h` / `Crc16.cpp` - CRC-16/CCITT-FALSE.
- `FrameCodec.h` / `FrameCodec.cpp` - `encodeFrame()` / `decodeFrame()`.

Tests: `firmware/grut-node/test/test_grut_protocol/test_main.cpp`
(run via `pio test -e native`).
