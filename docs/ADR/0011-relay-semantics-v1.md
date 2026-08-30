# 0011 — Relay v1 Semantics (Unicast, Single-Hop-Forward)

## Status

Accepted. **DESIGN/DOCUMENTATION ONLY.** No `RelayEngine` or forwarding
code exists yet, no firmware/source file is changed by this ADR, and
nothing in this ADR's subject matter is BUILD-TESTED or
HARDWARE-VERIFIED. Acceptance covers the semantics defined below as the
design Stage 6 implementation must follow; it does not mark any part of
Relay v1 as implemented.

## Context

Stage 5 gave GRUT the pieces a relay needs but never wired them together:

- `RouteTable` (ADR 0010) answers "for destination D, which next hop?"
  but has no producer of *indirect* routes (`destination != nextHop`)
  anywhere in the current runtime — only direct, one-hop routes are ever
  created (`gRouteTable.upsert(srcAddr, /*nextHop=*/srcAddr,
  /*hopCount=*/1, now)` in both `bridge_main.cpp` and
  `link_diag_main.cpp`).
- `EspNowDriver::sendToPeer(nextHopAddr, ...)` (ADR 0009) exists as a raw
  best-effort send-to-a-known-binding capability, but nothing currently
  calls it with a computed next hop — it is unused in production.
- `GrutFrameHeader` already carries `srcAddr`, `dstAddr`, `ttl`, and
  `sequence` on the wire (`docs/PROTOCOL.md`), but `ttl` is explicitly
  documented as "not acted upon yet" and `dstAddr` is not consulted by
  any receive-side logic in the production bridge.
- The current runtime is, and has only ever been, a **single-hop**
  two-node bridge. In that topology, "final destination" and "next hop"
  are always the same address for every frame either node has ever
  produced — the current code has never needed to distinguish those two
  concepts (§1).

### Evidence: gaps and inconsistencies in the current runtime

**`FrameReceiver` has no `dstAddr` check.** `FrameReceiver::poll()`
contains no check of `header.dstAddr` anywhere in its receive path; the
only gate is `dataSourceFilter_`, which inspects `header.srcAddr` only.
A `dstAddr`-based gate exists only in `link_diag_main.cpp`'s diagnostic
path, which does not run the UART bridge at all.

**`bridge_main.cpp` and `link_diag_main.cpp` disagree on `LinkManager`
feed gating.** `bridge_main.cpp`'s `observeValidFrame()` feeds
`gLinkManager` unconditionally for every valid frame regardless of
`srcAddr`, while `link_diag_main.cpp`'s equivalent inline logic gates
the same call on `header.srcAddr == grut::kPeerAddr` first.

**An unrecognized packet type is already dropped before UART delivery.**
Verified directly in `FrameCodec.cpp`/`FrameReceiver.cpp`:
`decodeFrame()` validates only `header.version`, not `header.type` — an
unrecognized type still decodes successfully (`DecodeResult::kOk`).
`FrameReceiver::poll()`'s dispatch, after the `kHeartbeat`/`kControl`
branches, ends with:

```cpp
if (header.type != static_cast<uint8_t>(grut::protocol::PacketType::kData)) {
  continue;  // unknown type - reserved, not handled yet
}
```

An old node encountering a frame of any type other than
`kData`/`kHeartbeat`/`kControl` already safely drops it before reaching
the `DataSourceFilter`/UART-write branch. This existing behavior is the
mechanism Decision §1 relies on to introduce a new packet type safely.

Conversely, reusing the *existing* `kData` type for transit (relayed)
frames would **not** be safe on an old node: an old `FrameReceiver` has
no `dstAddr` check and would treat a transit `kData` frame exactly like
ordinary local data, delivering it to UART the moment
`DataSourceFilter(srcAddr)` happens to pass — directly violating Issue
#3's acceptance criterion that transit DATA must never reach the relay
UART. This is why Decision §1 introduces a new, distinct packet type
rather than overloading `kData`.

All gaps above are reported as findings this ADR resolves, not silently
assumed away.

## Decision

### 1. A new packet type, `kRoutedData`, is required — `kData` is never relay-eligible

**Proposed new `PacketType` value (design only — `GrutProtocol.h` is
NOT modified by this ADR):**

```cpp
kRoutedData = 0x04  // proposed, not yet added to GrutProtocol.h
```

`0x04` is confirmed unused as a `PacketType` value anywhere in the
current codebase (it collides with nothing — `kFlagBroadcast = 0x04` is
a distinct enum, `GrutFlag`, occupying a different field).

**Semantics:**
- `kData` (`0x01`) remains exactly what it is today: legacy, single-hop-
  only opaque UART DATA. **`kData` is never relay-eligible** — a
  `kData` frame is always treated as local, exactly as today, on every
  node, forever, regardless of relay capability.
- `kRoutedData` (`0x04`, proposed) carries the same kind of opaque UART
  bytes, but explicitly means "routed unicast DATA with final-`dstAddr`
  semantics" (§2's LOCAL/TRANSIT classification applies to it, never to
  `kData`).
- `kRoutedData` may be originated, relayed, and finally delivered only
  by Relay-v1-aware nodes.
- `kHeartbeat`, `kControl`/`LINK_STATS`, and `kControl`/`HELLO` are never
  relayed.
- Broadcast `kData` **and** broadcast `kRoutedData` are both
  unsupported/reserved.

**Why this is preferable to a protocol-version bump:**
- Wire layout is unchanged — `kRoutedData` uses the exact same 9-byte
  header, same CRC, same field widths; it is an additive `PacketType`
  value, nothing else.
- This is an additive `PacketType` semantic extension, not a
  reinterpretation of an existing one.
- The relay path requires Relay-v1-aware participating nodes by
  construction — an old node simply never produces or forwards
  `kRoutedData`.
- The direct legacy `kData` two-node bridge remains completely unchanged
  — old and new nodes both still use `kData` for that path identically.
- **No protocol-version bump is required because the new routed payload
  has a backward-safe discriminator that an old `FrameReceiver` does not
  deliver to UART** — the evidence above confirms an unrecognized type
  is dropped before the UART-write branch is ever reached. An old node
  may still update generic observation/diagnostic state
  (`recordPeerBinding`, sequence-gap accounting, `validFrameObserver`)
  for a `kRoutedData` frame it doesn't understand, since those run
  unconditionally before type dispatch — so mixed-version *relay
  operation* is not functionally supported, but the *failure mode is
  safe*: no routed payload ever reaches an old node's UART by accident.

### 2. Relay eligibility

**Relay v1 forwards `kRoutedData` frames only** — never `kData`, never
`kHeartbeat`, never any `kControl` subtype — **and only when `dstAddr`
names a specific non-broadcast node other than the receiver.**

Justification:
- `kData` is excluded entirely, not merely "usually direct" — the type
  gate excludes it before `dstAddr` is even consulted, so a stray
  `kData` frame with a non-self `dstAddr` can never be forwarded.
- `kHeartbeat` is a link-local health signal (ADR 0007); `kControl`
  (`LINK_STATS`, `HELLO`) is discovery/control-plane traffic (ADR
  0007/0008) and HELLO is always broadcast — relaying discovery
  broadcasts is flooding/mesh territory, explicitly out of scope before
  Stage 7.
- `LINK_STATS` models exactly one point-to-point `LinkManager`
  relationship; forwarding it would misrepresent a link-local snapshot
  as saying something about a multi-hop path.
- **Broadcast `kRoutedData` is explicitly reserved/unsupported**, exactly
  like broadcast `kData` — no current code produces either, and neither
  is defined to mean anything for Relay v1.

The eligibility check is **type-and-destination-first**: packet type
must equal `kRoutedData` and `dstAddr` must be a specific non-broadcast,
non-self address before a frame is even considered a relay candidate.

### 3. Layer separation — Transport/FrameReceiver vs. future Networking/RelayEngine

Routing decisions, TTL handling, header rewrite, re-encoding, and
sending live in a new, not-yet-implemented `RelayEngine` component —
never inside `FrameReceiver` or `EspNowDriver`.

**`FrameReceiver`'s architectural *role* remains Transport** — it still
decodes, still does not make routing decisions, still does not itself
decide how a TRANSIT frame is forwarded. Its **implementation**
requires scoped changes to support Relay v1 correctly:
- LOCAL/TRANSIT classification for `kRoutedData` (§4)
- per-immediate-source/link sequence accounting, replacing today's
  single shared `expectedSequence_` (§6 — a real change, not a no-op)
- a bounded hand-off of TRANSIT `kRoutedData` frames to `RelayEngine`

None of these changes move routing/TTL/re-encode/send logic into
`FrameReceiver` — routing decisions stay confined to `RelayEngine`. Only
`FrameReceiver`'s role (decode and local delivery, not routing
decisions) is preserved architecturally; its code still requires the
changes above.

**Future Networking / `RelayEngine` (new component, not implemented in
this Issue):**
- relay-ingress authorization (§5a)
- route lookup, next-hop validation (§8)
- TTL decision/decrement (§7)
- `srcAddr`/sequence retransmit semantics (§6, §7)
- re-encode / CRC (`FrameCodec::encodeFrame`, reused)
- send to the resolved next hop (`EspNowDriver::sendToPeer`)
- relay diagnostic counters (§13)

### 4. Transit frame must not reach relay UART

With §1's new type and §3's classification: **a TRANSIT `kRoutedData`
frame (`dstAddr` names a specific other node) never reaches this node's
`UartTransport::send()` under any condition** — it is handed to
`RelayEngine` (or dropped per §5a/§8's preconditions) and never enters
the `DataSourceFilter`/UART-write branch. LOCAL `kRoutedData` frames
(`dstAddr == kOwnAddr`) go through the existing `DataSourceFilter`-then-
UART-write path, **with the additional final-destination authorization
requirement in §5b**. `kData` frames are entirely unaffected by any of
this — they are never classified, never handed to `RelayEngine`, and
always follow today's exact unmodified path.

**Compatibility with the current two-node deployment:** today's bridge
produces and consumes only `kData`, never `kRoutedData`, so this entire
mechanism is inert until a Relay-v1-aware node actually originates
`kRoutedData`.

### 5. `srcAddr` rewrite — with relay-ingress and final-destination authorization required (§5a, §5b)

**`srcAddr` is rewritten to the relay's own address on every
`kRoutedData` retransmission.** This keeps `NeighborTable`, `RouteTable`
Stage 5.2 direct-route population, and `EspNowDriver`'s endpoint-binding
conflict policy correct with zero changes: the relay itself makes
"srcAddr is the immediate transmitter" true by construction, for every
node that ever decodes a frame.

**This alone is not sufficient — a relay-admission gate is required
(§5a), not "any valid route/binding is relayed regardless of sender."**

#### 5a. Relay-ingress authorization

Because `srcAddr` is rewritten to the relay's own address, the final
node ends up trusting *the relay*, not whoever actually produced the
data. Without an ingress forwarding gate, **any** discovered or
misconfigured node feeding `kRoutedData` into AIR1 would have it turned
into apparently-authorized, AIR1-originated traffic by the time it
reaches AIR2 — silently defeating the Stage 4.2 authorization boundary
at the final destination.

**A new, separate Networking-layer policy is required:**

```cpp
// Conceptual signature - not implemented in this Issue.
using RelayIngressFilter = bool (*)(uint8_t immediateSrcAddr);
```

This is **explicitly not** `DataSourceFilter` — the two answer different
questions and must not be conflated:
- `DataSourceFilter` = whose **local** `kData`/`kRoutedData` may reach
  *this node's own* UART (existing, Stage 4.2, unchanged in meaning).
- `RelayIngressFilter` = whose **TRANSIT** `kRoutedData` this node is
  willing to forward on behalf of (new, Networking-layer, Relay v1).

**For the controlled H3 milestone:** AIR1 may relay `kRoutedData` only
when the immediate `srcAddr` (as decoded, i.e. the actual previous-hop
transmitter) equals GROUND's address. Discovery alone (`NeighborTable`)
never grants relay authorization — this mirrors, and does not weaken,
ADR 0008's existing discovery-vs-authorization separation. **No
cryptographic authentication or security is introduced** — this is
deterministic forwarding authorization only, exactly as ADR 0009's
binding policy and the existing `DataSourceFilter` are both explicitly
"not authentication or security."

**Required `RelayEngine` forwarding preconditions:**
1. packet type == `kRoutedData`
2. `dstAddr` is a specific unicast address, not self, not broadcast
3. immediate `srcAddr` (previous hop) passes `RelayIngressFilter`
4. `RouteTable::lookup(dstAddr, &entry)` succeeds
5. `RouteTable::isFresh(dstAddr, nowMs)` succeeds
6. `entry.nextHop != kOwnAddr`
7. `entry.nextHop != header.srcAddr` (as received) — no bounce-back
8. `entry.nextHop`'s **direct-neighbor freshness** holds — answerable
   today via the *existing* `NeighborTable::isFresh()` (§8)
9. `EspNowDriver::lookupPeerMac(entry.nextHop, &mac)` succeeds
   (existence only — see §8 for what remains a Stage 6.1 gap)
10. TTL permits forwarding (§7)

#### 5b. Final-destination authorization

At the true final destination (AIR2 in the controlled milestone),
**local delivery of `kRoutedData` still requires the existing Stage 4.2
`DataSourceFilter` to accept the immediate `srcAddr`** — the packet type
being `kRoutedData` rather than `kData` does not bypass or weaken this
in any way. For H3: AIR2 accepts final `kRoutedData` only from immediate
`srcAddr == AIR1`. AIR2 must therefore be configured to trust its
immediate relay/ingress peer, not an "original producer" identity that
no longer appears on the wire once `srcAddr` is rewritten per hop.

**`NodeConfig.h` limitation:** today's binary AIR/GROUND role scheme has
no slot for "trust this specific relay address," for either
`RelayIngressFilter` (on AIR1) or the final-destination
`DataSourceFilter` re-pointing (on AIR2) — both require a configuration-
scheme extension, recorded in "Required future implementation changes."

**Mixed-version consequence:** an old (pre-Relay-v1) final-destination
node **cannot** sit behind a new relay under any configuration — this is
not a reconfiguration gap, it is a hard capability gap. `kRoutedData` is
a new `PacketType` an old `FrameReceiver` does not recognize; per the
evidence above, an old node drops an unrecognized packet type before the
`DataSourceFilter`/UART-delivery branch is ever reached (it may still
update generic observation state — `recordPeerBinding`, sequence-gap
accounting, `validFrameObserver` — since those run unconditionally
before type dispatch, but the payload itself never reaches UART). **No
`DataSourceFilter` configuration on old firmware can change this**,
because old firmware never gets as far as consulting
`DataSourceFilter` for a `kRoutedData` frame at all.

**Every node participating in a `kRoutedData` routed path — the origin,
every relay, and the final destination — must therefore run
Relay-v1-aware firmware.** `DataSourceFilter` reconfiguration on the
final destination (§5b above) is **necessary** once that firmware is
Relay-v1-aware, but it is **not sufficient by itself** to make an old,
non-Relay-v1-aware final destination compatible — no configuration
change can substitute for the firmware update that adds `kRoutedData`
recognition and the LOCAL/TRANSIT classification in the first place.

For the controlled H3 milestone specifically: GROUND (origin), AIR1
(relay), and AIR2 (final destination) are all Relay-v1-aware firmware —
AIR2's `DataSourceFilter` authorizes immediate `srcAddr = AIR1`, and
AIR1's `RelayIngressFilter` authorizes immediate `srcAddr = GROUND` —
on top of that shared firmware capability, not instead of it.

### 6. Sequence semantics — per-immediate-link, not per-destination and not node-wide

**Current implementation:** one node-wide `SequenceGenerator` allocates
every outbound GRUT frame's `sequence` value on send, regardless of
destination or packet type; on receive, `FrameReceiver` tracks exactly
one `expectedSequence_`/`hasSequence_` pair, updated for every valid
decoded frame regardless of `srcAddr`. This is adequate for the
original, strict one-peer DATA bridge, where exactly one link and one
peer exist by construction. **It is not reliable the moment a node
observes more than one GRUT address** — this limitation is already
visible today via Stage 4.2 HELLO discovery (a node hearing two direct
neighbors already produces meaningless combined gap counts), independent
of whether any relay exists.

A relay node makes this unavoidable: it has, by definition, more than
one relevant direct link (its own configured peer, plus whatever it
relays toward). Consider GROUND physically sending both a frame with
`dstAddr = AIR1` and a `kRoutedData` frame with final `dstAddr = AIR2,
nextHop = AIR1` — **both travel over the same GROUND→AIR1 direct radio
link.** If sequence were allocated per *final destination* (one counter
for AIR1-addressed traffic, a different one for AIR2-addressed
`kRoutedData`), AIR1's receiver — which correctly tracks sequence per
physical link — would see two independent, interleaved sequence spaces
arriving on what is, at the radio level, a single link, and gap
accounting would be invalid again, just relocated to a different axis.

**Corrected model:**
- **Send:** one sequence context **per immediate unicast next-hop /
  direct link** — not per final destination, not one global node-wide
  counter. All unicast packet types a node sends over the *same*
  physical/direct link — legacy `kData`, `kRoutedData`, `kHeartbeat`,
  `LINK_STATS` — share that link's one sequence context.
- **Receive:** one sequence context **per immediate `srcAddr` / direct
  link**, for non-broadcast frames — keyed by the true one-hop
  transmitter (§5).
- **`dstAddr` (final destination) is never the sequence key, on either
  side.**
- **Broadcast HELLO uses a separate, bounded broadcast sequence
  context**, not any unicast link's context: it still carries a
  `sequence` field, but that value neither consumes nor is compared
  against any unicast link's `expectedSequence_` state. A broadcast
  frame's arrival is not evidence about a specific unicast link's gap
  count, even though it shares that link's `srcAddr`. No per-destination
  sequence state of any kind is introduced.

**Why this collapses correctly to today's exact behavior for a strict
two-node deployment:** for a deployment with exactly one authorized
unicast peer and no additional observed peers, "one context per
immediate link" is indistinguishable from today's one global counter for
unicast traffic — full backward compatibility for that specific case.
This is narrower than "every node running today's firmware" — **current
Stage 4.2 firmware can already observe multiple GRUT addresses via HELLO
even while only one peer is authorized for DATA** (§ evidence above);
such a node already has more than one observed address today, even
though it has only one unicast DATA peer. Legacy `kData` payload
semantics are entirely unchanged regardless of how many peers a node
observes — only sequence/loss *statistics* are in scope here, and those
may change/improve precisely on nodes that observe multiple peers or
broadcast HELLO traffic, since the per-link model gives such a node
correct per-source accounting for the first time. Mixed old/new loss
statistics remain unreliable, as already stated above.

**This is a real protocol-semantic change, not merely an implementation
fix:**
- Field width/layout is unchanged — `sequence` stays a 2-byte,
  little-endian counter.
- `sequence` **remains link-loss metadata** — its purpose does not
  change.
- What changes is the **allocation/accounting scope**: from today's
  single node-wide scope to **per-immediate-link** scope for
  Relay-v1-aware operation — a genuine change to what "gap-free" means
  for a stream of sequence values, stated as a protocol-semantic change
  without a wire-layout change.
- Old and new nodes still decode the `sequence` field identically — the
  byte value's meaning as a number is unchanged.
- **Mixed-version loss statistics are not compatible or reliable** — an
  old node's single global counter and a new relay-aware node's
  per-link counters do not produce comparable gap counts once more than
  one link/peer is involved.
- **A protocol-version bump is still not required — for payload-safety
  reasons, not because the sequence semantics are unchanged.** §1's
  `kRoutedData` discriminator is what prevents the dangerous consequence
  (transit payload reaching UART); the sequence-scope change is a real
  semantic change that mixed-version deployments must not rely on for
  correct statistics, but it does not, by itself, create a wire-decoding
  hazard the way reusing `kData` for transit would have.

### 7. TTL / hop-limit semantics — scoped to `kRoutedData`, re-encoding covers all mutated fields

**TTL is checked/decremented only for `kRoutedData` at a relay decision
point.** `kData` retains today's exact behavior (never inspected, never
relay-forwarded, ever) on every node.

1. **Arrival check:** if `ttl == 0` on a `kRoutedData` frame at a relay
   decision point, **drop**.
2. **Decrement:** otherwise, `newTtl = ttl - 1`.
3. **Post-decrement check:** if `newTtl == 0`, **drop**.
4. **Forward:** otherwise, set `header.ttl = newTtl`, rewrite `srcAddr`
   (§5) and allocate a new `sequence` value from the outbound link
   context for the chosen next hop (§6), then **re-encode the entire
   frame via `FrameCodec::encodeFrame()` and recompute CRC over all
   three mutated fields together** — `ttl`, `srcAddr`, and
   `sequence` — then `sendToPeer(nextHop, ...)`.

**Exact origin value, worked for the controlled milestone:** for a
route with `hopCount = N`, the originating node must set `initial_ttl
>= hopCount(destination)`. For `GROUND -> AIR1 -> AIR2` (`hopCount =
2`):

```
GROUND originates kRoutedData, final dst=AIR2, ttl=2
  -> physical nextHop=AIR1
AIR1 checks/decrements 2 -> 1, passes (5a) ingress + (§8) forwarding
  preconditions, rewrites srcAddr=AIR1 and allocates a fresh sequence
  from its AIR2-bound link context, re-encodes (ttl+srcAddr+sequence,
  CRC recomputed), sends to AIR2
AIR2 (final destination) receives ttl=1, applies no TTL check (relay-
  decision-point-only rule), passes 5b's DataSourceFilter check against
  srcAddr=AIR1, delivers to UART
```

`ttl == 0` is dropped on arrival at a relay decision point (step 1);
`ttl == 1` arriving at a relay decrements to `0` and is dropped (step
3) — a frame must arrive with `ttl >= 2` to survive being relayed
exactly once. `ttl` is `uint8_t`; step 1 excludes `0` before step 2 ever
subtracts, so the computation can never underflow — no path in this
design increments `ttl`. This algorithm applies only at a relay
decision point, never at the true final destination, which delivers
regardless of what `ttl` value arrives (including today's universal
`0`), preserving full backward compatibility with every frame the
current bridge produces today (confirmed: neither `FrameBuilder::flush()`
nor any of `bridge_main.cpp`'s send functions ever sets `header.ttl`).

**CRC re-encoding is mandatory:** `FrameCodec::encodeFrame()` must be
re-run over the mutated header — there is no in-place byte-patch path in
this codebase, and none should be added.

### 8. Route freshness and endpoint resolution

Forwarding preconditions are the full list in §5a. Next-hop liveness is
answerable today with the **existing** `NeighborTable::isFresh(address,
nowMs, staleAfterMs)` (`firmware/grut-node/lib/neighbor/NeighborTable.h`)
— no new API is required for that specific check. What remains a
genuine gap is narrower: `EspNowDriver::lookupPeerMac()` is
existence-only (its own doc comment: "regardless of freshness") — the
**endpoint binding** itself (the address↔MAC mapping) has no freshness
query, distinct from whether the *neighbor* is fresh. A next-hop could
be a fresh direct neighbor while its recorded MAC binding is old (though
still the correct MAC, absent a MAC change). **This binding-freshness
gap is the actual Stage 6.1 requirement.**

No RSSI metric is invented, consistent with ADR 0010. A stale/failed
send is counted (§13) and neither retried nor blocked on inside any
ESP-NOW callback.

### 9. Static indirect-route mechanism for the controlled milestone

- **On GROUND:** test-only injected static route, `destination = AIR2,
  nextHop = AIR1, hopCount = 2`, refreshed only while GROUND's own
  direct-neighbor observation of AIR1 is itself fresh — not a blind
  timer:

  ```
  On GROUND, in a dedicated relay-test-only build:
    when GROUND's own direct-neighbor observation of AIR1 is refreshed
    (the same moment GROUND's own direct route to AIR1 —
    destination=AIR1, nextHop=AIR1, hopCount=1 — is refreshed by the
    existing Stage 5.2 wiring):
      also upsert(destination=AIR2, nextHop=AIR1, hopCount=2, nowMs)
  ```

  This ties the injected route's freshness to AIR1's own observed
  liveness rather than an independent, disconnected timer, while
  remaining exactly what it is: a narrowly-scoped, test-build-only
  injection, not route advertisement — no node transmits anything new
  over the air to support this; GROUND only reacts to traffic it already
  receives from AIR1 for unrelated reasons. This must never be compiled
  into `bridge_main.cpp` or `link_diag_main.cpp`'s normal build
  environments.

- **On AIR1:** nothing needs to be injected. AIR1's route to AIR2,
  `destination = AIR2, nextHop = AIR2, hopCount = 1`, is the
  already-existing Stage 5.2 direct-route mechanism — no new mechanism,
  no test-only code, on AIR1 for this purpose. (Injecting
  `destination=AIR2, nextHop=AIR2, hopCount=2` on AIR1 would be
  internally inconsistent: a route whose `destination` equals its own
  `nextHop` is, by `RouteTable`'s own definition, a *direct* route and
  cannot simultaneously claim `hopCount > 1`.)

**Evidence boundary for this mechanism:** the direct-route population
*mechanism* (Stage 5.2 wiring, generic code shared by every node running
`bridge_main.cpp`/`link_diag_main.cpp`) is HARDWARE-VERIFIED — on the
specific GROUND-observing-AIR1-and-AIR2 test from Issue #1
(`docs/ADR/0010-route-policy-v1.md`). **AIR1 directly hearing AIR2 and
obtaining its own `AIR2 -> AIR2, hop=1` route has not itself been
hardware-verified** — the generic mechanism is proven, but this specific
node-pair relationship is a required H3 precondition (§10) that must be
verified in that hardware setup, not assumed to already hold.

### 10. H3 hardware-test precondition (documented only, not executed here)

GROUND's own Stage 5.2 wiring already creates a **direct** route for any
node it directly hears (`destination -> destination, hopCount=1`). Per
ADR 0010's route policy, a fresh direct route (`hopCount=1`) is not
replaced by a competing route to the same destination with an equal or
higher hop count while it stays fresh (policy case 4) — this specific
policy rests on the **existing BUILD-TESTED** `RouteTable` behavior
(host-test-proven, per ADR 0010's own test suite); the
competing-next-hop cases have **not** been exercised on real hardware
(`docs/ADR/0010-route-policy-v1.md` Status section is explicit about
this split). If GROUND could also hear AIR2 directly, GROUND's own
automatically-created direct route (`AIR2 -> AIR2, hop=1`) would win
over the injected indirect route (`AIR2 -> AIR1, hop=2`) per this
BUILD-TESTED policy the moment both exist — defeating the point of the
relay test.

**Required future H3 precondition, stated here and not executed in this
Issue:**
- GROUND can directly reach AIR1.
- AIR1 can directly reach AIR2 (itself an unverified precondition, §9).
- GROUND must **not** have a fresh direct route to AIR2 — the
  controlled test's physical/RF setup must ensure GROUND cannot hear
  AIR2 directly, specifically so the indirect route from §9 is the only
  route GROUND has to AIR2 and is actually exercised.

**What H3 will and will not hardware-test, stated precisely:** because
H3 deliberately ensures GROUND has no fresh direct route to AIR2, no
competing route to the same destination ever exists on GROUND during
H3 — there is nothing for ADR 0010's policy case 4 to arbitrate between.
H3 will hardware-test:
- `GROUND -> AIR1 -> AIR2` routed forwarding end-to-end
- the injected indirect route `AIR2 -> AIR1, hop=2` on GROUND (§9)
- AIR1's direct `AIR2 -> AIR2, hop=1` route, in this specific hardware
  topology (§9's open precondition)
- TTL, relay-ingress/final-destination authorization (§5a/§5b), and
  relay-forwarding behavior generally

H3 will **not** hardware-test `RouteTable`'s competing-next-hop policy
(cases 3–5) — that would require GROUND to hold two live, competing
routes to AIR2 simultaneously, which H3's own precondition above rules
out by design. **ADR 0010's competing-next-hop cases therefore remain
IMPLEMENTED + BUILD-TESTED only after H3**, exactly as they are today,
unless a separate hardware test is later designed specifically to
exercise route competition (e.g., a topology where GROUND is allowed a
fresh direct route to AIR2 alongside the indirect one). No hardware test
is performed as part of Issue #3.

### 11. Originating routed-send path

Static `RouteTable` state alone does not create outbound traffic.
GROUND originating a frame with final `dstAddr = AIR2` but radio next
hop `AIR1` requires a send-side routed path that does not exist in any
form today — `FrameBuilder` currently builds every `kData` frame with a
single, constructor-fixed `dstAddr_` (its one configured peer) and hands
it straight to `EspNowDriver::send()`, which itself targets a single,
constructor-fixed `peerMac_`. Neither consults `RouteTable` at all.

Recorded as a required future Stage 6 implementation item, kept as its
own layer:

```
opaque UART bytes
  -> FrameBuilder-equivalent assembles a kRoutedData frame,
     final dstAddr = AIR2
  -> Networking TX layer: RouteTable::lookup(AIR2) -> nextHop = AIR1
  -> EspNowDriver::lookupPeerMac(AIR1) -> AIR1's MAC
  -> EspNowDriver::sendToPeer(AIR1, frame)
```

Final destination and radio next hop remain **separate** at every step
— this does not re-introduce routing logic inside `FrameBuilder`/
`EspNowDriver`. No MAVLink parsing is introduced by this path. For the
controlled Stage 6 milestone, "final destination = AIR2" on GROUND's
side may be a hardcoded test-build constant — no dynamic vehicle
selection or MAVLink-derived addressing is required for this milestone.
`kData`'s existing origination path is completely unaffected.

### 12. Loop-prevention boundary

**What Relay v1 guarantees:**
- No frame loops forever: TTL strictly decreases (§7) and is dropped the
  moment it would reach zero, with wraparound impossible by
  construction — bounding the worst case to `initial_ttl - 1` hops.
- The immediate ping-pong case is prevented by §5a's precondition 7.

**What Relay v1 explicitly does not guarantee — remains Stage 7:**
- General loop-free routing across an arbitrary topology; a longer cycle
  is bounded by TTL, not detected or prevented from starting.
- No duplicate-frame detection or suppression (§5's consequence —
  `srcAddr` no longer identifies the true originator once relayed).
- No protection against two independently relay-capable nodes both
  choosing to forward the same frame toward the same destination. The
  controlled Stage 6 milestone has exactly one relay-capable node in the
  path, so this cannot arise in H3; it is not solved for any richer
  topology.
- No route convergence guarantees — routes are static test-injected
  (§9), not dynamically discovered.

### 13. Relay observability

A future `RelayEngine` must expose bounded, monotonically increasing
counters, at minimum:

- transit `kRoutedData` frames seen
- forwarded
- relay-ingress-authorization rejects (§5a)
- TTL-expired drops (§7)
- no-route / stale-route drops (§5a preconditions 4–5)
- next-hop-not-fresh drops (§5a precondition 8, using the existing
  `NeighborTable::isFresh()`)
- no-endpoint drops (§5a precondition 9)
- loop/back-to-source rejects (§5a precondition 7)
- send busy/failure drops

No ASCII diagnostics into the transported UART stream; no blocking
retries inside any ESP-NOW callback.

### 14. Callback and resource rules

All relay decision-making lives in the future `RelayEngine`, called from
ordinary main-loop context, never inside
`EspNowDriver::onReceive()`/`onSend()`.

`RouteTable`/`NeighborTable`/binding-table lookups are bounded,
fixed-capacity array scans (`kMaxRoutes = 8`, `kMaxNeighbors = 8`,
`kMaxPeerBindings = 8`) — mechanically loops, but bounded,
constant-time-in-practice ones. One relay decision per received frame
does **no unbounded loops**, no heap growth, no blocking wait — every
lookup is a bounded scan over a fixed-size array whose maximum size is a
compile-time constant, and every buffer used is a fixed-size,
stack-allocated array (the same pattern `FrameReceiver::poll()` already
uses).

## Consequences

**Positive:**
- `NeighborTable`, `RouteTable` Stage 5.2 direct-route wiring, and
  `EspNowDriver`'s endpoint-binding conflict policy still require zero
  changes (§5's rewrite-`srcAddr` choice).
- The new `kRoutedData` type (§1) closes a mixed-version UART-safety
  hole using an already-existing, already-verified codec/receiver
  behavior rather than a new mechanism.
- `NeighborTable::isFresh()` already answers next-hop liveness (§8) —
  one fewer new API than a naive design would need.

**Trade-offs / constraints:**
- The existing Transport/Networking responsibility boundary is
  preserved; scoped implementation changes inside `FrameReceiver` are
  still required (§3).
- No end-to-end originator identity survives at the GRUT layer once
  relayed (§5).
- DATA authorization requires reconfiguration on both ends, on top of
  Relay-v1-aware firmware everywhere in the path (§5b) — not a
  substitute for it: `RelayIngressFilter` on the relay (§5a) and a
  re-pointed `DataSourceFilter` on the final destination (§5b).
- Sequence/loss accounting is a real protocol-semantic change in scope
  (per-immediate-link, not per-destination, not node-wide) — not
  "implementation correctness only" (§6).
- No loop or duplicate detection beyond TTL bounding (§12).
- The H3 milestone verifies more than the underlying mechanism alone
  already proves — AIR1↔AIR2 direct reachability still needs its own
  hardware exercise (§9). **The competing-route policy is explicitly
  not exercised by H3** — H3's own precondition (§10) rules out any
  competing route existing on GROUND during the test, so ADR 0010's
  competing-next-hop cases remain BUILD-TESTED only after H3, pending a
  separately designed test.

## Alternatives considered

- **Reuse `kData` for transit frames.** Rejected (§1): an old node's
  `FrameReceiver` has no `dstAddr` check and would deliver a transit
  `kData` frame to its own UART the moment `DataSourceFilter` happens to
  pass — a real safety violation of Issue #3's core acceptance
  criterion.
- **Bump the protocol version instead of adding `kRoutedData`.**
  Rejected (§1): the existing "unrecognized type is dropped before
  UART" behavior already provides a safe, additive discriminator; a
  version bump would be a strictly larger, unnecessary change for the
  same safety property.
- **Preserve `srcAddr` end-to-end instead of rewriting it per hop.**
  Rejected (§5): would require a new mechanism to distinguish "immediate
  transmitter" from "original sender" for `NeighborTable`/`RouteTable`/
  binding-table observation, with real mixed-version risk.
- **Embed relay decision logic inside `FrameReceiver` directly.**
  Rejected (§3): violates the existing Transport/Networking layer
  separation this codebase has maintained since ADR 0001.
- **Reuse the existing `DataSourceFilter` as the relay-admission check.**
  Rejected (§5a): that filter's established meaning is "whose data may
  reach *this node's own* UART," a different question from "whose
  frames may I forward elsewhere."
- **Skip relay-ingress authorization; rely only on route/binding
  existing.** Rejected (§5a): would let any discovered or misconfigured
  node inject traffic that becomes indistinguishable from the relay's
  own authorized traffic once `srcAddr` is rewritten.
- **Allocate sequence per final destination.** Rejected (§6): breaks gap
  accounting for any physical link carrying frames toward more than one
  final destination, which is exactly what a relay's upstream link does
  by construction.
- **Inject the static test route on AIR1 instead of GROUND.** Rejected
  (§9): internally inconsistent — a `destination == nextHop` entry
  cannot also claim `hopCount > 1`; AIR1's real relationship to AIR2 is
  a normal, already-existing direct route.
- **Refresh the static test route on an unconditional periodic timer.**
  Rejected (§9): makes `RouteTable::isFresh()` meaningless by decoupling
  the route's freshness from whether its next hop is actually alive.
- **Treat broadcast `kData`/`kRoutedData` as an implicit local-delivery
  case.** Rejected (§2): would silently define behavior for an unused,
  unconsidered case rather than deciding it on purpose.
- **General route advertisement for the controlled milestone.** Rejected
  (§9): explicitly out of scope — Stage 7 (mesh) territory.

## Compatibility impact

**1. Wire-layout compatibility: unchanged.** No header size change, no
CRC algorithm change, no protocol version bump. `kRoutedData` is a new,
additive `PacketType` enum value, not a layout change.

**2. Field-semantic compatibility: changed.**
- `dstAddr` as final destination is a new Stage 6.0 decision, not
  something the prior single-hop-only system demonstrated on its own —
  in that topology, final destination and next hop have always been the
  same address.
- `ttl` gains active meaning, scoped to `kRoutedData` only (§7) — `kData`
  is completely unaffected.
- `srcAddr` becomes hop-local for `kRoutedData` specifically (§5) —
  `kData`'s `srcAddr` meaning is completely unaffected.
- `sequence`'s allocation/accounting **scope** genuinely changes (node-
  wide → per-immediate-link) for Relay-v1-aware nodes — a
  protocol-semantic change, not "implementation correctness" (§6).
  Legacy `kData` payload semantics are entirely unchanged by this. For a
  strict two-node deployment with one unicast peer and no additional
  observed peers, the new per-link unicast sequence model collapses to
  the old global model for unicast traffic; a node that already observes
  multiple peers or broadcast HELLO traffic may see its sequence/loss
  statistics change or improve, since the per-link model gives it
  correct per-source accounting for the first time. Mixed old/new loss
  statistics remain unreliable, as stated in category 3 below.
- `kRoutedData` itself is a new field-semantic surface (§1).

**3. Loss-statistics compatibility: not unaffected.** Mixed old/new
deployments, or any node handling more than one direct link, cannot
produce comparable gap statistics under the current single-global-
counter architecture. This must be fixed (§6) before Relay v1's
statistics, or any statistics on a node with more than one relevant
link, can be trusted.

**4. Relay-path mixed-version / deployment compatibility: a firmware
capability gap, not merely a configuration gap.** An old
(pre-Relay-v1) final-destination node **cannot** sit behind a new relay
under any configuration — it drops the unrecognized `kRoutedData`
`PacketType` before its `DataSourceFilter` is ever consulted (§5b), so
no amount of reconfiguring an old node's trusted-source address changes
anything. Every node in a `kRoutedData` path — origin, every relay, and
final destination — must run Relay-v1-aware firmware. Once a node *is*
Relay-v1-aware, it additionally requires configuration: (a) the final
destination must be reconfigured to trust the relay's address as its
`DataSourceFilter` source (§5b), and (b) the relay itself must be
configured with a `RelayIngressFilter` trusting the correct upstream
peer (§5a) — neither of which today's binary AIR/GROUND `NodeConfig.h`
scheme has a slot for. Configuration is necessary on top of
Relay-v1-aware firmware; it is not a substitute for it.

**Protocol version bump: not required.** Category 1 is unaffected.
Category 2's `kRoutedData` addition is additive and safe-by-construction
(§1's evidence); its `sequence`-scope component is a real semantic
change but does not affect wire decoding — every node, old or new,
still parses the same bytes into the same field values. Category 3's
cost is a statistics-reliability fix, not a decoding concern. Category
4's cost is configuration/deployment, enforced by existing application-
level policy layers (`DataSourceFilter`, proposed `RelayIngressFilter`),
not by the decoder. No new required field, flag, or backward-
incompatible reinterpretation of an existing field is introduced
anywhere in this design — only one new, additive `PacketType` value and
configuration-layer requirements.

## Required future implementation changes (explicitly out of scope for this Issue)

1. **`GrutProtocol.h`:** add `kRoutedData = 0x04` to the `PacketType`
   enum (§1) — the one actual wire-visible addition this design
   requires, still not made in this Issue.
2. **`FrameReceiver`:** add LOCAL/TRANSIT classification for
   `kRoutedData` (§3, §4) and the hand-off to `RelayEngine` — `kData`'s
   existing path is untouched.
3. **New `RelayEngine` component:** relay-ingress authorization (§5a),
   route lookup, TTL decision, `srcAddr`/sequence rewrite, re-encode,
   `sendToPeer`, diagnostic counters (§3, §7, §13).
4. **New `RelayIngressFilter` mechanism** (§5a) — separate from
   `DataSourceFilter`, configured per relay node.
5. **Final-destination `DataSourceFilter` reconfiguration** (§5b) — to
   trust the immediate relay's address, not an "original producer."
6. **Originating routed-TX path** (§11) — `RouteTable`-aware send logic
   for `kRoutedData`, kept separate from `FrameBuilder`/`EspNowDriver`;
   the ability to set a non-default, non-zero `ttl` on `kRoutedData`
   frames.
7. **`NodeConfig.h`/configuration scheme:** extend beyond the current
   binary AIR/GROUND role model to support both `RelayIngressFilter`
   configuration and a relay-aware `DataSourceFilter` target (§5a, §5b).
8. **Sequence/`LinkManager` refactor (§6):** per-immediate-link (not
   per-destination) send-side allocation; per-immediate-`srcAddr`/link
   receive-side gap tracking; a separate bounded broadcast sequence
   context for HELLO, excluded from unicast link accounting;
   `LinkManager` generalized to multiple relevant links.
9. **`EspNowDriver` binding-freshness (Stage 6.1, narrow scope):** only
   the endpoint-binding freshness query is a genuine gap — next-hop
   *neighbor* freshness is already answerable via the existing
   `NeighborTable::isFresh()` (§8).
10. **Static test-route injection (§9):** relay-test-only build,
    correctly placed on GROUND, freshness-coupled to AIR1's observed
    liveness.
11. **H3 hardware preconditions (§9, §10):** AIR1 directly hearing AIR2
    (`AIR2 -> AIR2, hop=1` at AIR1) has not been hardware-verified and
    must be, alongside GROUND's physical/RF setup ensuring it has no
    fresh direct route to AIR2. **H3 does not exercise ADR 0010's
    competing-next-hop policy (cases 3–5)** — those remain IMPLEMENTED +
    BUILD-TESTED only after H3, pending a separately designed hardware
    test that deliberately allows a competing route to exist.

## Acceptance-criteria cross-check (Issue #3)

Final-destination-vs-next-hop as a Stage 6.0 decision (§1); relay-
eligible types scoped to the new `kRoutedData` only, with `kData`
explicitly and permanently excluded (§1, §2); transit-never-reaches-UART
via the new type plus corrected layering (§3, §4); `srcAddr` behavior
with both relay-ingress (§5a) and final-destination (§5b) authorization
required; sequence behavior correctly scoped to per-immediate-link (not
per-destination, not node-wide) and classified as a protocol-semantic
change (§6); TTL exactness scoped to `kRoutedData`, with CRC re-encoding
covering `ttl`+`srcAddr`+`sequence` together (§7); route/endpoint
preconditions including the existing `NeighborTable::isFresh()` (§8);
the controlled-milestone mechanism with accurately-scoped Stage 5.2/
RouteTable evidence (§9, §10); the originating routed-send path (§11);
no forwarding/mesh/ACK/MAVLink/BIOS/flashing introduced anywhere in this
Issue; relay observability including ingress-rejection counters (§13);
bounded-loop resource wording (§14); and compatibility impact across all
four categories (above). **DESIGN/DOCUMENTATION ONLY — not
BUILD-TESTED, not HARDWARE-VERIFIED.**
