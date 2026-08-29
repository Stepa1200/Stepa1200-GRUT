# 0008 — HELLO Discovery Subtype and DATA-Source Safety Gate

## Status

Accepted (documents already-implemented, hardware-verified behavior;
see Compatibility impact for scope).

## Context

Stage 4.0–4.1 gave GROUND a `NeighborTable` fed only by frames from the
one statically configured peer (`grut::kPeerAddr`). To let GROUND
discover more than one AIR node on the same ESP-NOW channel (Stage
4.2), two separate problems needed solving:

1. **How does a node announce its presence** to nodes it has no prior
   fixed-peer relationship with, without inventing a new GRUT packet
   type or touching the wire header?
2. **Does being discovered mean being trusted with DATA?** The
   existing `FrameReceiver::poll()` wrote every `kData` payload
   straight to UART regardless of `srcAddr`. Once more than one node
   could be heard, that unconditionally merges any second node's byte
   stream into the one physical UART Mission Planner/the FC is
   reading — corrupting MAVLink with no warning.

Stage 4.2 needed both general discovery and a separate DATA
authorization gate to keep newly visible nodes from automatically
becoming UART DATA sources. This ADR documents both the discovery
mechanism and the safety gate together because the second is
meaningless in isolation: shipping HELLO without the gate would
reopen the vulnerability it closes.

A related, narrower issue — `link_diag_main.cpp`'s own frame-observer
filtering silently hiding discovered nodes from `NeighborTable` — was
found and fixed on real hardware during Stage 4.2 testing; see
Consequences below for that specific fix.

## Decision

### HELLO as a `kControl` subtype, not a new packet type

`GrutFrameHeader.type` already has `kControl` (0x03), and
`link::LinkStatsCodec` already established the pattern of a
subtype byte as the first byte of a CONTROL payload
(`kControlSubtypeLinkStats = 0x01`). HELLO reuses this exact pattern:

```cpp
// lib/discovery/HelloCodec.h
constexpr uint8_t kControlSubtypeHello = 0x02;
constexpr size_t kHelloPayloadSize = 1;
```

The HELLO payload is **exactly one byte** — the subtype tag itself.
No sender-identity field is included in the payload, because identity
is already present in every GRUT frame's header (`srcAddr`), and
`NeighborTable`/`EspNowDriver`'s peer-binding table (ADR 0009) are
both already keyed on that field. Duplicating it inside the payload
would add bytes for no benefit.

HELLO frames set `dstAddr = grut::protocol::kBroadcastAddress` (a
constant that already existed in `GrutProtocol.h` before this work,
unused until now) and are sent via a new best-effort,
non-queued/non-retried path, `EspNowDriver::sendBroadcastIfIdle()`,
gated by the same "quiet UART + `txIdle()`" check already used for
LINK_STATS (`bridge_main.cpp`'s `kLinkStatsUartQuietMs`, reused
as-is). `EspNowDriver::start()`/`stop()` register/deregister the
ESP-NOW broadcast MAC (`FF:FF:FF:FF:FF:FF`) as an additional peer
alongside the fixed unicast peer, per Espressif's own guidance that a
broadcast destination should be registered before sending to it.

HELLO is sent periodically (`kHelloIntervalMs`, currently 2000ms —
explicitly noted in code as a first, provisional choice, not derived
from a specific measurement) from `bridge_main.cpp`, on **every**
node/role, not just AIR — the same shared `bridge_main.cpp` binary
runs on AIR and GROUND alike, so discovery is symmetric by
construction, not a GROUND-only mechanism.

### Discovery is generic; DATA authorization is not

`FrameReceiver::poll()` already called `validFrameObserver_()`
unconditionally for **any** successfully decoded frame, regardless of
type or `srcAddr` — this is what feeds `NeighborTable::onFrameObserved()`
in `bridge_main.cpp`'s `observeValidFrame()`, and it required no change
to observe HELLO: a HELLO frame is a normal, successfully-decoded GRUT
frame like any other, from `FrameReceiver`'s point of view.

The gap was specifically in the **kData handling path**, which wrote
payload bytes to UART with no source check at all. The fix is a new,
optional predicate on `FrameReceiver`:

```cpp
// include/transport/FrameReceiver.h
using DataSourceFilter = bool (*)(uint8_t srcAddr);
```

Passed as an optional constructor parameter (default `nullptr`,
meaning "accept every source" — the exact behavior before this gate
existed). When non-null, `poll()` calls it **only** for `kData` frames,
immediately before the UART write:

```cpp
if (dataSourceFilter_ != nullptr && !dataSourceFilter_(header.srcAddr)) {
  ++dataWrongSourceDrops_;
  continue;  // dropped, never reaches UART
}
```

`bridge_main.cpp` supplies the actual policy:

```cpp
bool acceptDataSource(uint8_t srcAddr) {
  return srcAddr == grut::kPeerAddr;
}
```

reusing the **already-existing** static peer-address configuration —
no second identity source was introduced. `FrameReceiver` itself never
learns *why* an address is accepted; `NeighborTable` is never
consulted by this gate and has no say in DATA authorization. Discovery
(who is visible) and DATA authorization (whose bytes reach UART)
are deliberately two separate, independently-owned decisions.

`link_diag_main.cpp` does not use `FrameReceiver` at all (it decodes
frames inline in its own `processReceivedFrames()`), so it is
unaffected by this constructor-parameter change and needed no
modification for the gate itself.

## Consequences

- A discovered-but-unauthorized node's DATA is silently dropped and
  counted (`dataWrongSourceDropCount()`), never merged into the UART
  stream. The gate authorizes **only by GRUT `srcAddr`**
  (`return srcAddr == grut::kPeerAddr;`) — it prevents DATA from a
  *different* GRUT address from reaching UART. It does **not**
  authenticate the sender's MAC and cannot distinguish a
  second/misconfigured/malicious node that transmits DATA while using
  the same authorized GRUT `srcAddr` from the genuine one. This is a
  documented limitation of the current policy, not a security
  guarantee — closing it belongs to a future SecurityManager/binding
  decision, not this ADR.
- Active DATA-source authorization remains a **static, compile-time**
  decision (`grut::kPeerAddr`) for this stage. There is no dynamic
  "active peer switching" — that is explicitly out of scope here and
  belongs to a later stage/ADR if ever pursued.
- Every node — not just GROUND — now periodically transmits HELLO,
  adding one more low-frequency contender for the single shared
  ESP-NOW send slot (already under active investigation for the
  separate LINK_STATS/parameter-load regression, see the open item in
  `PROJECT_ROADMAP.md`). HELLO reuses the exact same idle-gating
  discipline as LINK_STATS specifically to avoid making that
  contention worse; it does not eliminate it.
- `link_diag_main.cpp` originally combined its own NeighborTable-feed
  and LinkManager-feed behind one shared `srcAddr == kPeerAddr` filter,
  which — found and fixed during Stage 4.2 hardware testing — silently
  hid every discovered node except the one fixed peer. The fix split
  this into two independently-scoped checks: `NeighborTable` observes
  any valid address; `LinkManager` (which models exactly one fixed
  peer relationship and has no meaning for any other address) stays
  scoped to `kPeerAddr`. This is now the template for keeping
  discovery and any single-peer-scoped subsystem correctly separated
  in future diagnostic or runtime code.

## Alternatives considered

- **A dedicated new `PacketType` for HELLO** instead of a `kControl`
  subtype. Rejected: would require a wire-format-visible enum change
  and does not distinguish itself from CONTROL's existing purpose
  (out-of-band management traffic) in any way that matters.
- **Reusing existing HEARTBEAT traffic for discovery** (since it is
  already periodic and already updates `NeighborTable` via the generic
  `observeValidFrame()` path). Rejected as the *primary* mechanism:
  HEARTBEAT is unicast to one configured peer, so it would not
  naturally reach an as-yet-unknown node without also making
  HEARTBEAT broadcast — conflating a link-health signal (which
  `LinkManager` scopes to one peer on purpose) with a general-presence
  announcement. HELLO stays a distinct, explicit signal for exactly
  this reason.
- **Filtering `kData` inside `NeighborTable`** rather than adding a
  separate predicate to `FrameReceiver`. Rejected per the explicit
  layering requirement (`NeighborTable` must not become a DATA-
  authorization or ESP-NOW-specific component) — the same principle
  ADR 0009 restates for endpoint bindings.

## Compatibility impact

Compatibility impact of *this ADR-writing task itself*: **NONE** — no
code changed while writing this document. The following describes the
compatibility **nature of the already-implemented Stage 4.2 decision**
it documents.

**Backward-compatible, additive CONTROL-payload extension** — not "no
protocol semantics added." `kControlSubtypeHello = 0x02` is a new,
additive subtype value carried inside the already-existing `kControl`
packet type, alongside the pre-existing `kControlSubtypeLinkStats =
0x01`. It uses fields that already existed in the wire format before
this work (`kBroadcastAddress` as `dstAddr`, `kFlagBroadcast`) but were
previously unused. No header width, CRC, protocol version, addressing
field width, or sequence field changed. The existing `LinkStatsCodec`
decoder rejects the 1-byte HELLO payload because its required length
and subtype do not match `LINK_STATS`'s own — that is the concrete,
evidenced behavior; this ADR does not generalize it into a guarantee
about every possible old or future receiver's behavior.

Native test coverage: `test_hello_codec` — 13 host tests (codec
correctness, a full `GrutFrameHeader`+`FrameCodec` round trip, and
integration with `NeighborTable`) — and `test_data_source_gate` — 6
isolated host tests using the real `FrameCodec`/protocol types but a
local `GateHarness` reimplementation of the dispatch fragment under
test, **not** the production `FrameReceiver`+`EspNowDriver` path.
Commit `fdd8156` records real-hardware verification of the Stage 4.2
safety-gate/discovery behavior (see below) — this is the evidence for
the production path, not a separately committed test.

Hardware-verified (GRUT commit `fdd8156`, the Stage 4.2 implementation
commit — HELLO broadcast discovery, the DATA-source safety gate, the
`link_diag_main.cpp` source-scoping bug fix, and its own native/build
results and hardware verification all landed together in this one
commit): direct AIR1↔GROUND bridge unaffected (Mission Planner link
quality 100% after the gate was added), and both `NEIGHBOR id=1` (real
peer) and `NEIGHBOR id=3` (discovered, non-authorized peer) observed
simultaneously in diagnostic output.
