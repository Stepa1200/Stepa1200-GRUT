# Transport frame pipeline and BIOS freeze

## Status

Accepted

## Context

BIOS (boot, diagnostics, console, RuntimeManager-driven UART lifecycle)
has reached a stable, fully tested state: ADR 0001 and ADR 0002 are
implemented, exclusive UART ownership works and is covered by native
tests, and the console commands needed to hand UART to Transport
(`transport status/start/stop`) exist. Continuing to extend BIOS at
this point would compete for attention with the actual goal of the
project - a working data bridge - and risks destabilizing a module that
already works.

At the same time, GRUT Transport (ADR 0001) has so far been only a
disabled stub and a real-but-empty `UartTransport` that claims the UART
peripheral without moving any data. The project needs its first real
bridge: MAVLink bytes from a flight controller's UART, carried over
ESP-NOW to a second node, and delivered back out over that node's UART
to a ground station (Mission Planner). Building this requires an
internal pipeline inside Transport, and a decision on whether the
wire-level packet format is tied to ESP-NOW specifically or is an
independent protocol that ESP-NOW merely carries.

## Decision

- **BIOS is frozen.** No new BIOS features. Only bug fixes are
  in scope for BIOS from this point on. All new development happens
  under `firmware/grut-node/src/transport/`.
- **Transport is built as a pipeline** with this fixed layer order for
  the first bridge milestone, **v0.2.0 - "UART over ESP-NOW Bridge"**:

  ```
  UART Driver
      |
      v
  Frame Builder
      |
      v
  ESP-NOW Driver
      |
      v
  Frame Receiver
      |
      v
  UART Driver
  ```

  No mesh, no routing, no relay at this stage. The only goal is a
  working `MAVLink -> ESP #1 -> (ESP-NOW) -> ESP #2 -> Mission Planner`
  path between exactly two nodes.

- **GRUT defines its own wire protocol**, documented in
  `docs/PROTOCOL.md`, independent of ESP-NOW. ESP-NOW is treated as one
  possible carrier for GRUT frames, not as the protocol itself. The
  protocol document covers: header format, protocol version, packet
  types, addressing, CRC, TTL, control messages, heartbeat, and control
  flags.

- **ESP8285 uses the ESP-NOW v1.0 packet-size model.** A single ESP-NOW
  application payload must not exceed 250 bytes, including the entire
  serialized GRUT frame header and payload. Therefore the maximum GRUT
  data payload is:

  ```
  MAX_GRUT_PAYLOAD = 250 - sizeof(GrutFrameHeader)
  ```

  The implementation must enforce this with a compile-time assertion,
  e.g.:

  ```cpp
  static_assert(
      sizeof(GrutFrameHeader) + kMaxPayload <= 250,
      "GRUT frame exceeds ESP-NOW v1 payload limit");
  ```

- **UART is treated as an opaque byte stream.** GRUT frames do not need
  to align with MAVLink message boundaries. The Frame Builder chunks
  available UART bytes into bounded GRUT DATA frames (e.g. 180-220
  bytes of payload each), and the receiver writes payload bytes back to
  UART in sequence order. General-purpose fragmentation/reassembly of
  logical messages is out of scope for v0.2.0; it will be introduced
  only for GRUT control messages or future transports that require
  logical messages larger than one frame.

- **v0.2.0 scope is explicitly limited:**
  - Exactly two directly communicating nodes.
  - Nodes use explicitly configured roles for the first hardware test:
    `AIR` and `GROUND`. Automatic role detection is not part of this
    milestone.
  - Broadcast may be used for discovery, but DATA frames use a known
    peer MAC once pairing is established.
  - The ESP-NOW receive callback must not perform UART writes,
    parsing, logging, retransmission, or other lengthy work. It only
    validates minimal bounds and enqueues received data for processing
    in the main loop.
  - A new ESP-NOW transmission is started only after the previous send
    callback completes.

- **Roadmap order is fixed** and must not be skipped ahead of:
  1. UART over ESP-NOW Bridge (this milestone, no mesh).
  2. LinkManager: RSSI, packet loss tracking, reconnect, statistics.
  3. Neighbor table, routing, relay, mesh - only after 1 and 2 are
     working.

## Consequences

**Positive:**

- Effort concentrates on the one thing that actually proves the
  project works end to end: a live data bridge between two nodes.
- A stable, tested BIOS is not put at risk by unrelated Transport
  development churn.
- Defining GRUT's own protocol independent of ESP-NOW means the same
  framing/addressing/CRC logic can later run over UDP, Ethernet, or
  LoRa without being rewritten - only the driver layer changes.
- Treating UART as an opaque byte stream (rather than requiring
  frame-per-MAVLink-message alignment) significantly simplifies the
  first implementation: the Frame Builder never needs to parse MAVLink.
- The compile-time payload assertion turns the ESP-NOW size limit into
  a build-time guarantee instead of a runtime risk discovered in the
  field.
- The explicit receive-callback and send-ordering constraints follow
  Espressif's own guidance that ESP-NOW callbacks run in a
  high-priority Wi-Fi context; keeping callbacks minimal and
  serializing sends avoids callback ordering issues and dropped
  packets.
- The fixed pipeline order and roadmap sequencing prevent scope creep
  (e.g. starting mesh/routing work before a single link is proven
  reliable).

**Trade-offs / constraints:**

- Any BIOS change proposed during this phase must be justified as a
  bug fix, not a feature, or it needs its own explicit approval to
  unfreeze BIOS temporarily.
- The GRUT wire protocol's header/CRC/addressing format is a
  compatibility-critical decision: once nodes are flashed and deployed
  with a given format version, changing it breaks interoperability
  between old and new firmware. This makes early protocol design
  decisions (recorded in `docs/PROTOCOL.md`) higher-stakes than most
  other code changes.
- Chunking UART as an opaque byte stream means a single MAVLink message
  can span two GRUT frames; if a frame is lost, the reassembled UART
  stream on the receiving side will contain a gap, which the MAVLink
  parser downstream (Mission Planner) must tolerate on its own - GRUT
  v0.2.0 does not guarantee lossless delivery.
- Explicit AIR/GROUND role configuration (no auto-detection) means
  every node must be told its role before the bridge works; this is a
  manual step until a later milestone adds auto-detection.
- Deferring LinkManager and mesh means the first bridge will have no
  reconnect or loss-recovery behavior beyond what MAVLink itself
  tolerates - acceptable for proving the concept, not for field use.
