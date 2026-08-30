# GRUT Wire Protocol

## Status

This document is the human-readable companion to
`firmware/grut-node/lib/grut_protocol/` - if they ever disagree, the
code is the source of truth for exact field widths, and this document
should be corrected to match it.

**Implemented and HARDWARE-VERIFIED** (see `PROJECT_ROADMAP.md` for the
per-stage evidence detail):
- Frame header, packet types, flags, CRC, encoder, decoder, ESP-NOW v1
  size limit, ESP-NOW driver, UART chunking (FrameBuilder/FrameReceiver)
  - Stage 1.
- `LinkManager` (link health, recovery policy) - Stage 2.
- Packet-loss statistics and `LINK_STATS` reporting - Stage 3.
- Neighbor discovery (`HELLO`) and `NeighborTable` - Stage 4.
- Multi-peer endpoint plumbing (GRUT address <-> ESP-NOW MAC binding) -
  Stage 5.0, for policy cases 1-2 (new binding, same-MAC refresh); cases
  3-4 (fresh-conflict rejection, stale rebind) are IMPLEMENTED +
  BUILD-TESTED only, not yet hardware-exercised (ADR 0009).
- Direct route population in runtime (`destination == nextHop, hop=1`,
  created automatically from direct observation) - Stage 5.2.

**Implemented and BUILD-TESTED, not yet HARDWARE-VERIFIED:**
- `RouteTable` core (Stage 5.1) as an isolated component.
- `RouteTable`'s competing-next-hop policy (cases 3-5: lower-hop-count
  replacement, no-flap-on-equal-or-higher-hop-count, stale-route
  replacement) - no runtime producer of indirect routes exists yet, so
  no hardware scenario has exercised these cases (ADR 0010). Do not
  treat Stage 5.2's hardware result as covering these cases too.

**NOT implemented (any form):**
- Routed forwarding / `RelayEngine` - proposed design only, Accepted but
  not implemented (`docs/ADR/0011-relay-semantics-v1.md`, Stage 6.0).
- Mesh / dynamic routing / route advertisement - Stage 7, not started.
- Retransmission or acknowledgement - out of scope since ADR 0005, not
  scheduled.

See ADR 0005 for the full original roadmap and `PROJECT_ROADMAP.md` for
current per-stage status.

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
| 3      | `srcAddr`       | uint8   | 1    | Sender's node address. For `kData` this is, and always will be, the direct single-hop sender - unaffected by anything below. `docs/ADR/0011-relay-semantics-v1.md` (Accepted design, not yet implemented) *decides* that for the proposed `kRoutedData` type only, this field means the *immediate previous-hop transmitter*, rewritten by each relay on retransmission - not a wire-layout change, only a new protocol-semantic decision scoped to `kRoutedData`. |
| 4      | `dstAddr`       | uint8   | 1    | Destination node address, or `0xFF` (broadcast). `docs/ADR/0011-relay-semantics-v1.md` (Accepted design, not yet implemented) *decides* this is the *final* GRUT destination for `kRoutedData`, not a next hop - `RouteTable`/endpoint-binding next-hop information is always local and never serialized here. The current single-hop-only runtime cannot itself distinguish "final destination" from "next hop" for `kData`, since every `kData` frame it has ever produced has always had the same address in both roles; this is a new decision for the proposed `kRoutedData` type, not something the existing code demonstrates. |
| 5      | `ttl`           | uint8   | 1    | Time-to-live. Not acted upon by any code today (no relay/mesh implemented yet). `docs/ADR/0011-relay-semantics-v1.md` (Accepted design, not yet implemented) defines the exact future decrement/drop algorithm a Relay v1 implementation must follow, scoped to the proposed `kRoutedData` type only - see "Relay v1 semantics" below - but no code currently reads or decrements this field for any type. |
| 6-7    | `sequence`      | uint16  | 2    | Link-loss metadata, little-endian on the wire, wrapping `65535 -> 0`. See "Sequence allocation and accounting scope" below for the current implemented model versus the proposed Relay-v1-aware model - the two differ in allocation/accounting scope, not in field width or wire meaning. |
| 8      | `payloadLength` | uint8   | 1    | Number of payload bytes following the header. Set automatically by the encoder; used by the decoder to locate the CRC trailer. |

## Sequence allocation and accounting scope

**CURRENT IMPLEMENTATION (v0.2.0, all shipped firmware today):** one
node-wide `SequenceGenerator` allocates every outbound GRUT frame's
`sequence` value, regardless of packet type or destination. On receive,
`FrameReceiver` tracks exactly one global `expectedSequence_`/
`hasSequence_` pair, updated for every valid decoded frame regardless of
`srcAddr`.

- Adequate for the original, strict one-peer DATA bridge, where exactly
  one link and one peer exist by construction.
- **Not reliable for multi-peer observation or Relay operation.** This
  limitation already becomes visible once a node hears more than one
  GRUT address - the current production bridge can already observe
  frames from multiple addresses via Stage 4.2 HELLO/discovery, and
  feeds every valid frame into the same one global receive-sequence /
  `LinkManager` path regardless of which address it came from. This is
  true today, independent of whether any relay exists.

**PROPOSED RELAY-V1 MODEL (`docs/ADR/0011-relay-semantics-v1.md`,
Accepted design, not yet implemented):**
- **Send:** one unicast sequence context **per immediate next-hop /
  direct link**, not per final destination and not one global counter.
  All unicast packet types sent over the *same* physical/direct link -
  legacy `kData`, proposed `kRoutedData`, `kHeartbeat`, `LINK_STATS` -
  share that one link's sequence context.
- **Receive:** one unicast sequence context **per immediate `srcAddr` /
  direct link**, for non-broadcast frames only.
- **Broadcast (HELLO) uses a separate, bounded broadcast sequence
  context**, not any unicast link's context: a broadcast frame still
  carries a `sequence` field, but that value neither consumes nor
  perturbs any unicast link's `expectedSequence_`/loss-accounting state.
  No per-destination sequence state of any kind is introduced.
- For a strict two-node deployment with one unicast peer and no
  additional observed peers, the new per-link unicast sequence model
  collapses to the old global model for unicast traffic - full backward
  compatibility for that specific case. This is narrower than "every
  node running today's firmware": current Stage 4.2 firmware can already
  observe multiple GRUT addresses via HELLO even while only one peer is
  authorized for DATA, so such a node already has more than one observed
  address today. Legacy `kData` payload semantics are unchanged
  regardless; sequence/loss *statistics* may change or improve on nodes
  that observe multiple peers or broadcast HELLO traffic, since the
  per-link model gives such a node correct per-source accounting for the
  first time. Mixed old/new loss statistics remain unreliable, as
  already documented above.
- This is a **protocol-semantic change in allocation/accounting scope**
  (node-wide -> per-immediate-link), not a wire-layout change and not
  merely an implementation-detail fix - see the ADR's Compatibility
  impact section for the full analysis, including why this does not
  require a protocol version bump.

The in-memory `GrutFrameHeader` C++ struct (see `GrutProtocol.h`) uses
the same field names and logical widths, but its `sizeof()` is **not**
the wire size (compiler padding may differ - currently 10 bytes in
memory vs. 9 on the wire). The header is always serialized/deserialized
field-by-field by `FrameCodec`, never memcpy'd as a raw struct.

## Packet types

| Value | Name        | Meaning |
|-------|-------------|---------|
| `0x01`| `kData`     | An opaque chunk of the UART byte stream. No relationship to MAVLink message boundaries - see [UART as an opaque byte stream](#uart-as-an-opaque-byte-stream). **Never relay-eligible, on any node, ever** - always treated as local single-hop traffic, unaffected by the proposed Relay v1 design. Unicast only; `dstAddr == kBroadcastAddress` for `kData` is reserved/unsupported. |
| `0x02`| `kHeartbeat`| Empty heartbeat frame used by LinkManager supervision. |
| `0x03`| `kControl`  | Control/management frame. `LINK_STATS` (`0x01`) and `HELLO` (`0x02`) are the formal payload subtypes defined so far. |
| `0x04`| `kRoutedData` (**proposed, NOT added to `GrutProtocol.h` yet**) | `docs/ADR/0011-relay-semantics-v1.md` proposes this new type for routed unicast opaque UART DATA with final-`dstAddr`/relay semantics (§1-§14 of the ADR), kept distinct from `kData` specifically so an old node's already-existing "unrecognized packet type is dropped before UART" behavior makes mixed-version operation fail safely rather than mis-deliver transit payload. Broadcast `kRoutedData` is reserved/unsupported, same as `kData`. |


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
| 0x04 | `kFlagBroadcast`   | Informational: frame's `dstAddr` is `kBroadcastAddress`. Set by HELLO (see below); not enforced by the codec for any packet type. |

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

See "Relay v1 semantics" below for the exact algorithm a future
implementation must use (arrival check, decrement, post-decrement check,
mandatory CRC re-encoding) - defined in `docs/ADR/0011-relay-semantics-v1.md`,
not yet implemented by any code in this repository.

## Relay v1 semantics (Accepted design, ADR 0011 - not yet implemented)

`docs/ADR/0011-relay-semantics-v1.md` defines, but does not implement,
the semantics Relay v1 must follow for forwarding a routed unicast
payload whose final `dstAddr` names a node other than the receiver. This
section summarizes the accepted design; see the ADR for full
justification, evidence, and required future implementation changes.

- A **new, proposed `PacketType` value, `kRoutedData` (`0x04`), not yet
  added to `GrutProtocol.h`**, is the only relay-eligible traffic.
  **`kData` is never relay-eligible, on any node, ever** - kept as
  legacy, always-local, single-hop-only traffic, specifically so an old
  node's already-existing "unrecognized packet type is dropped before
  UART" behavior (confirmed in the current `FrameReceiver`/`FrameCodec`)
  makes mixed-version operation fail safely if `kRoutedData` is ever
  seen by old firmware, rather than mis-delivering transit payload to
  UART.
- `kHeartbeat`, all `kControl` subtypes (`LINK_STATS`, `HELLO`), and
  broadcast `kRoutedData`/`kData` are never forwarded.
- Relay decision-making (relay-ingress authorization, route lookup, TTL
  handling, re-encode, send) is proposed to live in a new, not-yet-
  implemented `RelayEngine` component - not inside `FrameReceiver` or
  `EspNowDriver`.
- A transit `kRoutedData` frame must never reach the relay node's own
  UART - `FrameReceiver` classifies it TRANSIT and hands it to
  `RelayEngine`, independent of the existing Stage 4.2
  `DataSourceFilter`.
- **Relay-ingress authorization is required, separate from
  `DataSourceFilter`:** a proposed `RelayIngressFilter` governs which
  immediate upstream peer's `kRoutedData` a relay is willing to forward
  at all - without it, `srcAddr` rewriting (below) would let any
  discovered/misconfigured node's traffic become indistinguishable from
  the relay's own authorized traffic once relayed. The final
  destination's existing `DataSourceFilter` is also not bypassed by the
  new packet type - on a Relay-v1-aware final destination, it must be
  reconfigured to trust the relay's address specifically. **This
  reconfiguration is necessary but not sufficient for an old
  (pre-Relay-v1) final-destination node** - an old node drops the
  unrecognized `kRoutedData` type before `DataSourceFilter` is even
  consulted, so no configuration change makes old firmware compatible;
  every node in a routed path (origin, every relay, and the final
  destination) must run Relay-v1-aware firmware.
- `srcAddr` is proposed to be rewritten to the relay's own address on
  `kRoutedData` retransmission - hop-local, not end-to-end origin
  identity.
- `sequence` allocation/accounting scope is proposed to become
  **per-immediate-link** (send: per next-hop; receive: per `srcAddr`),
  not per final destination and not the current node-wide model - see
  "Sequence allocation and accounting scope" above. This is a genuine
  protocol-semantic change, not merely an implementation fix, though it
  requires no wire-layout change and no version bump.
- `ttl` is checked and decremented only for `kRoutedData`, only at a
  relay decision point, never at the true final destination (which
  accepts any `ttl` value, including today's universal `0`, for
  backward compatibility). The originating node must set `initial_ttl
  >= hopCount(destination)`. Any relay mutation re-encodes and
  recomputes CRC over **all** mutated fields together - `ttl`,
  `srcAddr`, and `sequence`.
- No route advertisement, dynamic routing, mesh, ACK/retransmission, or
  cryptographic authentication is introduced by Relay v1.

This section will be updated again once Relay v1 is actually implemented
with known BUILD-TESTED/HARDWARE-VERIFIED status; until then it
describes the accepted design, not an implemented behavior.

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

**CURRENT IMPLEMENTED DIRECT-MODE BEHAVIOR:** both packet types consume
the same node-wide `sequence` counter as `kData`; this was required so a
receiver could use one authoritative GRUT-frame sequence for link-loss
accounting in a topology with exactly one peer. See "Sequence allocation
and accounting scope" above for the proposed Relay-v1 per-immediate-link
model this behavior is scoped against once more than one relevant link
exists.

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

### HELLO discovery payload v1

The second formal `kControl` subtype is `HELLO` (`subtype = 0x02`). It
announces this node's presence for neighbor discovery. HELLO frames
are broadcast (`dstAddr = kBroadcastAddress`, `kFlagBroadcast` set)
and, like all control frames, are never forwarded to the opaque UART
byte stream.

HELLO's own subtype-specific handling is discovery-only: decoding a
HELLO payload confirms nothing beyond "this is a well-formed HELLO."
Separately, the *enclosing* GRUT frame - any valid frame of any type,
not only HELLO - already feeds generic observation state on receipt:
`NeighborTable` (who is directly visible), the GRUT-address-to-ESP-NOW-
MAC endpoint binding table, and direct-route population in
`RouteTable`. HELLO's practical role is to give a node something to
send when it has no other traffic yet, so that generic observation
pipeline has a frame to observe even before any DATA or heartbeat
exists between two nodes.

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | control subtype = `0x02` (`HELLO`) |

Total payload size: **1 byte**; serialized GRUT frame size: **12 bytes**
(9-byte header + 1-byte payload + 2-byte CRC).

Sender identity is not duplicated inside the payload - it comes from
the enclosing frame header's `srcAddr`, the same field every other
GRUT frame already carries. HELLO carries no other field by design.

Compatibility: this is a second, additive `kControl` subtype alongside
`LINK_STATS` (`0x01`) - it does not change the GRUT frame header, CRC,
or protocol version. A receiver that does not recognize subtype `0x02`
simply fails to decode it as anything meaningful (for example,
`LinkStatsCodec::decodeLinkStats()` rejects it on length/subtype
mismatch); it does not misinterpret it as a different, valid payload.

Discovery (who is visible) and DATA-source authorization (whose bytes
reach UART) are two separate decisions - being discovered via HELLO
does not by itself authorize a node's `kData` frames onto UART. See
`docs/ADR/0008-hello-discovery-and-data-source-safety-gate.md` for the
full design rationale.

### Sequence compatibility note

Before LinkManager work, bridge firmware assigned advancing sequence
numbers only to `kData`; temporary heartbeat/control frames therefore
usually carried zero. From the node-wide sequence change onward, every
outbound GRUT frame consumes one sequence value. The wire layout and
protocol version remain unchanged, but AIR and GROUND should be updated
as a pair: mixing old and new sequence semantics makes loss counters
ambiguous even though UART payload transport itself remains decodable.

**Known limitation, sharpened by Relay v1's accepted design (ADR 0011,
not yet implemented):** today's single shared `expectedSequence_` on
receive and single shared `SequenceGenerator` on send both assume
exactly one relevant direct peer/link. This already produces unreliable
gap statistics for any node that hears more than one direct neighbor
(e.g. via Stage 4.2 HELLO discovery), independent of relay. ADR 0011
proposes a per-immediate-link fix - per-`srcAddr`/link receive tracking,
per-next-hop/link send allocation (**not** per final destination - see
"Sequence allocation and accounting scope" above for why per-destination
allocation was considered and rejected) - as a required future
implementation change, and states this explicitly as a protocol-semantic
scope change, not merely an implementation-detail fix. Not yet done, and
not required for the existing two-node bridge to keep working, since
that topology has never had more than one relevant peer/link.

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
