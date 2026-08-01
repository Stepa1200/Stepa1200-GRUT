# ESP-NOW driver design

## Status

Accepted

## Context

ADR 0005 fixed the v0.2.0 pipeline (`UART Driver -> Frame Builder ->
ESP-NOW Driver -> Frame Receiver -> UART Driver`) and several policy
constraints (exactly two nodes, explicit AIR/GROUND roles, minimal
receive callback, serialized sends). It left four concrete engineering
questions open, all of which affect the internal structure of the
ESP-NOW Driver and are cheap to get wrong once code exists:

1. ESP-NOW on the ESP8266/ESP8285 Arduino core requires Wi-Fi to be in
   a specific mode and both nodes to share a radio channel, even though
   no access point is involved. Nothing has decided who sets this or
   what the channel is.
2. `esp_now_send()` needs a peer MAC address. ADR 0005 already ruled
   out dynamic pairing for this milestone, but did not say how a node
   learns its peer's MAC.
3. ADR 0005 requires the ESP-NOW receive callback to do nothing but
   validate bounds and enqueue - but no queue depth or overflow
   behavior was defined.
4. ADR 0005 requires serialized sends ("a new transmission is started
   only after the previous send callback completes") - but nothing
   defined what happens to frames produced while a send is still in
   flight.

## Decision

- **Wi-Fi mode and channel.** `EspNowDriver::start()` calls
  `WiFi.mode(WIFI_STA)`, then `WiFi.disconnect()` (so the station never
  attempts to associate with a router), then fixes the radio channel
  via `wifi_set_channel()` before calling `esp_now_init()`. The channel
  is a single named constant, `kEspNowChannel = 1`, defined once in the
  driver. Both nodes in a pair must build with the same channel value -
  this milestone does not implement channel negotiation or scanning.

- **Peer addressing via build-time configuration, not dynamic
  pairing.** Each node's role (`AIR`/`GROUND`) and its peer's MAC
  address are supplied as PlatformIO build flags on a **per-node
  PlatformIO environment** (e.g. `env:esp8285-air`,
  `env:esp8285-ground`), added alongside the existing generic
  `env:esp8285` (which remains for BIOS-only testing and is unchanged).
  Example:

  ```ini
  [env:esp8285-air]
  extends = env:esp8285
  build_flags =
      ${env:esp8285.build_flags}
      -D GRUT_NODE_ROLE=GRUT_ROLE_AIR
      -D GRUT_PEER_MAC={0x24,0x6F,0x28,0xAA,0xBB,0xCC}
  ```

  There is no runtime pairing handshake in this milestone: a node only
  ever unicasts to the one peer MAC it was built with.

- **Bounded receive queue, drop-newest on overflow.** The ESP-NOW
  receive callback copies the raw received bytes into a fixed-depth
  ring buffer (`kEspNowQueueDepth = 4` frames) and returns immediately
  - no parsing, no UART access, no logging, matching ADR 0005. If the
  queue is full when a new frame arrives, that incoming frame is
  dropped and a drop counter is incremented; the queue itself is never
  evicted to make room. Actual frame decoding (via `FrameCodec`)
  happens later, when the main loop drains the queue.

- **Bounded send queue, drop-newest on overflow, one send in flight.**
  Frames produced by the Frame Builder are pushed into a second
  fixed-depth ring buffer (`kEspNowQueueDepth = 4`, same constant/size
  as the receive queue for symmetry). The driver tracks a single
  `sendInFlight_` flag; `esp_now_send()` is called for the next queued
  frame only after the previous send's callback has fired. If the send
  queue is full when a new frame is ready, that frame is dropped (drop
  counter incremented) rather than blocking UART reading - there is no
  backpressure mechanism in this milestone. This is consistent with
  ADR 0005's existing acknowledgment that v0.2.0 does not guarantee
  lossless delivery.

## Consequences

**Positive:**

- All four questions have a single, written answer instead of being
  decided ad hoc while writing driver code, or differently by whoever
  touches it next.
- Fixed channel + build-time peer MAC keeps the first bridge's driver
  simple - no scanning, no handshake state machine, no dynamic peer
  table (that belongs to a future mesh milestone, per ADR 0005's
  roadmap ordering).
- Per-node PlatformIO environments make "which firmware goes on which
  physical node" an explicit, reviewable build configuration
  (`env:esp8285-air` vs `env:esp8285-ground`) rather than a runtime
  guess or a manually-edited source file per flash.
- Symmetric, fixed-depth queues with a simple drop-newest policy are
  easy to reason about, easy to test natively (bounded, deterministic
  behavior), and match ESP-NOW's own guidance to keep callbacks short.

**Trade-offs / constraints:**

- No channel negotiation means both nodes must be reflashed together
  if the channel constant ever changes - acceptable for a two-node
  bridge, not for a larger deployment.
- Build-time peer MAC means pairing a new pair of nodes requires a
  rebuild/reflash with updated `build_flags`, not a runtime pairing
  step. This is intentional for v0.2.0 but is a real operational
  limitation until a future pairing milestone.
- Drop-newest on both queues means bursts of traffic (in either
  direction) lose data silently beyond the 4-frame buffer, with only a
  counter to observe it - there is no retry, and no signal is sent back
  to the UART side to slow down. This is consistent with, and expected
  given, ADR 0005's "no lossless delivery guarantee" for v0.2.0.
- A queue depth of 4 is a starting guess sized for ESP8285's limited
  RAM (frames up to 250 bytes each; 4 slots per queue x 2 queues =
  up to ~2 KB), not a measured value - it may need tuning once real
  traffic patterns (MAVLink stream rate) are observed on hardware.
