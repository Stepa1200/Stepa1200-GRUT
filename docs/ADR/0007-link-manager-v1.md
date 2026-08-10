# LinkManager v1

## Status

Accepted

## Context

The first UART-over-ESP-NOW bridge (ADR 0005/0006) is working and
verified on real hardware, but it has no notion of link health: no way
to know whether the peer is currently reachable, how much data is
being lost, or how to recover after a period of loss without a full
teardown/reinit. ADR 0005's roadmap places this next, after the first
bridge and before mesh/routing.

Manual bring-up diagnostics (temporary `kControl`-frame stats, sequence
gap counting in `FrameReceiver`) already proved two things empirically:
the transport has a non-zero, measurable frame loss rate (~4% in the
byte-perfect test), and per-frame sequence numbers are enough to
quantify it precisely. That ad hoc mechanism needs to become a
permanent, well-defined subsystem instead of a debug flag that has to
be remembered to turn off.

A real technical constraint shapes this ADR: **RSSI is not obtainable
from ESP8266's ESP-NOW receive callback.** Unlike ESP32 (which passes
an `esp_now_recv_info_t` with `rx_ctrl.rssi` since ESP-IDF v5.1),
ESP8266's `esp_now_recv_cb_t` signature is fixed as
`(u8* mac, u8* data, u8 len)` with no packet metadata at all. The
documented ESP32 workaround (Wi-Fi promiscuous-mode sniffing of the
underlying 802.11 action frames) has no confirmed working equivalent
for ESP8266 in any source reviewed. Chasing RSSI on this chip would
mean committing to unproven low-level Wi-Fi driver hacking with no
guarantee of success, for a metric that isn't required to answer the
one question that matters operationally: is data getting through.

## Decision

**LinkManager v1 scope is exactly:**
- Heartbeat supervision (freshness tracking).
- Sequence-gap-based loss accounting for GRUT frames.
- A link state machine.
- A staged (non-destructive-first) recovery ladder.
- A statistics snapshot, exported only as a GRUT control frame (no new
  console, no TCP).

**Explicitly out of scope for v1:**
- RSSI - unsupported on ESP8266 via the official ESP-NOW API (see
  Context). Revisit only if a confirmed, working low-level technique
  for this specific chip is found; not pursued speculatively.
- A TCP console or any new interactive interface.
- Mesh, routing, relay (ADR 0005's roadmap keeps these later).
- Any failsafe *action* (RTL, LOITER, LAND, etc.) - LinkManager reports
  link state, it does not decide what the vehicle does about it. That
  decision belongs to a future, separate Control Authority / Failsafe
  layer that consumes LinkManager's state as an input. This separation
  is deliberate: link health monitoring and flight-safety decision
  making are different concerns with different consequences for
  getting them wrong, and must not be coupled in one module.

### Link state machine

```
LINK_UNKNOWN -> LINK_UP -> LINK_DEGRADED -> LINK_DOWN -> RECOVERING -> LINK_UP
```

`LINK_UNKNOWN` is the state before the first heartbeat is ever seen.
Assuming a heartbeat interval of 1000 ms:

| State | Condition |
|---|---|
| `LINK_UP` | heartbeat age < 2500 ms |
| `LINK_DEGRADED` | heartbeat age < 3000 ms, but short-window loss > 5% |
| `LINK_DOWN` | heartbeat age >= 3000 ms |
| `RECOVERING` | after `LINK_DOWN`, at least one heartbeat received but fewer than 3 consecutive successful heartbeats received yet |

`RECOVERING -> LINK_UP` only after 3 consecutive successful heartbeats
(not just one) - this avoids flapping back to `LINK_UP` on a single
lucky packet during an unstable recovery.

### Loss accounting

Loss is computed **only from GRUT frame sequence numbers**, never from
raw UART/MAVLink byte counts - the sequence number is the one
authoritative, protocol-level signal for "was a frame lost between the
two nodes," and it is already present in every frame header
(`docs/PROTOCOL.md`). Tracked fields:

- `expectedSequence`
- `receivedSequence`
- `gapCount`
- `receivedCount`
- `lossPercent` (derived: `gapCount / (gapCount + receivedCount)`)

Two windows are kept simultaneously:

- **Short window: 10 seconds** - drives `LINK_DEGRADED` detection
  (fast reaction to a link that just started degrading).
- **Long window: since boot** - overall picture, exported in the
  statistics snapshot, not used for state transitions.

### Recovery ladder

Timers are measured from the moment `LINK_DOWN` was entered (i.e. from
the last valid heartbeat - see the rule below), escalating through
these steps - never jumping straight to a full reinit:

```
t = 3 s without heartbeat  -> LINK_DOWN (already defined above)
t = 5 s                    -> verify peer is still registered with the driver
t = 8 s                    -> removePeer(); addPeer();  (re-register only)
t = 15 s                   -> full ESP-NOW reinit (WiFi mode/channel/esp_now_init from scratch)

After any step (including a full reinit): keep sending heartbeat
probes as normal. 3 consecutive heartbeats received -> LINK_UP.
```

These values are deliberately conservative: short enough that a real
outage is escalated within 15 seconds end to end, long enough that a
brief radio hiccup does not trigger peer re-registration or a full
reinit unnecessarily.

Two rules apply throughout the whole ladder, stated explicitly because
getting either wrong would silently break the design's intent:

- **Recovery timers are measured from the last valid heartbeat, not
  from the last arbitrary ESP-NOW frame.** A `kControl` stats frame or
  any other traffic must not reset the down-timer - only a heartbeat
  counts, because heartbeat freshness is the one signal this whole
  state machine is built around. Letting any frame type reset the
  timer would let the transport look "alive" while the specific
  channel this state machine cares about (heartbeat) has actually gone
  silent.
- **Any valid heartbeat received during recovery resets the recovery
  ladder** - i.e. jumps back to step "t = 3 s without heartbeat" being
  the current baseline again, not "continue counting toward the next
  ladder step from before." However, this does **not** mean an
  immediate return to `LINK_UP`: full `UP` status is only granted after
  3 consecutive heartbeats (see the state table above), so a single
  heartbeat during `RECOVERING` prevents further escalation but does
  not by itself declare the link healthy.

### Statistics export

A `LINK_STATS` payload, carried as a `PacketType::kControl` frame
(the same reserved type already used for the temporary bring-up stats
mechanism this milestone formalizes), containing: current link state,
short-window loss, long-window loss, heartbeat age, and the raw
counters above. No new transport, no console - this rides the existing
bridge.

## Consequences

**Positive:**

- Converts empirically-proven-useful ad hoc debug code (sequence gap
  counting, stats-over-control-frame) into a permanent, well-defined
  subsystem instead of a flag someone has to remember to disable.
- The state machine gives every future consumer (a Control Authority
  layer, a desktop UI, a log) one authoritative, well-defined signal
  for link health instead of everyone re-deriving it from raw counters.
- The staged recovery ladder avoids unnecessary full ESP-NOW
  reinitialization (Wi-Fi mode/channel reset) for transient loss,
  which is disruptive and slow relative to a lighter peer
  re-registration.
- Explicitly deferring RSSI avoids sinking time into an unproven,
  chip-specific low-level Wi-Fi hack for a metric that is not needed
  to answer "is the link usable" - loss and heartbeat freshness already
  answer that.
- Separating "report link state" from "decide what to do about it"
  keeps LinkManager testable and reusable independent of any future
  failsafe policy, and keeps flight-safety decisions in one clearly
  identified, auditable place rather than scattered into transport code.

**Trade-offs / constraints:**

- Without RSSI, LinkManager cannot distinguish "signal is getting weak
  but still mostly working" from "signal is fine but something else is
  dropping frames" - it only sees the aggregate effect (loss), not the
  cause. Diagnosing root causes of degradation may still require manual
  investigation (as this bring-up phase already demonstrated).
- The recovery ladder's timings (3s/5s/8s/15s) are fixed values chosen
  conservatively for a first implementation, not derived from measured
  flight data - they may need tuning once real flight-test loss
  patterns are observed. Changing them later is a small, contained
  edit (not an architecture change), since they are isolated constants
  in this ADR.
- Any future Control Authority / Failsafe layer is now a hard
  dependency for LinkManager's output to have real-world consequences.
  Until that layer exists, LinkManager's state is informational only -
  this is intentional for v1, but means v1 alone does not make the
  vehicle any safer, only more observable.
