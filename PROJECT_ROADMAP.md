# PROJECT_ROADMAP.md

> Purpose: define the staged GRUT engineering path and explicit exit
> criteria. Do not skip stages to solve a later problem prematurely.
> This file is the single source of truth for stage status. Do not
> duplicate this table elsewhere.

# 1. Engineering goal

Build GRUT incrementally into a reliable modular communication system.

Order of priority:

```text
make one link work
-> measure it
-> recover it
-> discover neighbors
-> route
-> relay
-> mesh
-> integrate operator/multi-drone workflows
```

Reliability and observability come before feature count.

# 2. Stage roadmap

## Stage 1 — Stable UART <-> ESP-NOW <-> UART bridge

**Status:** HARDWARE-VERIFIED

### Goal

Transport arbitrary UART bytes bidirectionally without parsing MAVLink.

### Exit criteria

- AIR UART -> GRUT -> ESP-NOW -> GROUND UART works.
- GROUND UART -> GRUT -> ESP-NOW -> AIR UART works.
- no diagnostic ASCII is injected into the transported UART stream.
- Mission Planner can communicate through the bridge.

---

## Stage 2 — LinkManager

**Status:** HARDWARE-VERIFIED

### Goal

Observe direct-link health without implementing routing.

### Exit criteria

- heartbeat/liveness state machine works.
- sequence gaps are measurable.
- send failures / queue drops are observable.
- recovery state is deterministic.
- LinkManager does not own routing.

---

## Stage 3 — Packet-loss statistics and recovery policy

**Status:** HARDWARE-VERIFIED

### Goal

Measure and recover a direct point-to-point link before adding
networking complexity.

### Exit criteria

- short-term and long-term loss are observable.
- degraded/down/recovering transitions are reproducible.
- passive recovery behavior is hardware verified.
- active reconnect/reboot ladders are not added without evidence that
  passive recovery is insufficient.

### Open question — one earlier RF-distance measurement

One earlier RF-distance test produced a result inconsistent with
surrounding measurements. A power-supply explanation was proposed but
is **NEEDS MEASUREMENT / INFERRED** — no reproducible source for this
in the repository or a specific hardware log. Do not treat that
earlier result as clean range data, and do not treat the proposed
explanation as CONFIRMED either, until a repository reference or a new
reproducible measurement exists.

---

## Stage 4 — Neighbor discovery / NeighborTable

**Status:** HARDWARE-VERIFIED

### Stage 4.0 — NeighborTable core

Fixed-size, host-testable direct-neighbor table.

### Stage 4.1 — Diagnostic visibility

Expose direct-neighbor state in dedicated diagnostic firmware.

### Stage 4.2 — HELLO discovery + DATA-source safety gate

- HELLO control subtype discovers multiple nodes.
- discovery does not by itself grant DATA authorization.
- the gate authorizes `kData` frames by GRUT `srcAddr` only
  (`srcAddr == grut::kPeerAddr`): DATA from a different GRUT address
  is rejected before reaching UART.
- this is not MAC authentication or security — two senders using the
  same authorized GRUT `srcAddr` cannot currently be distinguished by
  this gate. See `docs/ADR/0008-hello-discovery-and-data-source-safety-gate.md`
  for the full limitation.

### Exit criteria

GROUND can see AIR1 + AIR2 as separate GRUT neighbors simultaneously
on real hardware.

---

## Stage 5 — Routing foundation

### Stage 5.0 — Multi-peer endpoint plumbing

**Status:** HARDWARE-VERIFIED

### Goal

Preserve ESP-NOW source MAC metadata and maintain a fixed-size
mapping:

```text
GRUT next-hop address -> ESP-NOW MAC
```

Transport owns carrier endpoints; NeighborTable remains MAC-agnostic.

### Endpoint conflict policy v1

- new GRUT address + MAC -> create binding
- same GRUT address + same MAC -> refresh
- same GRUT address + different MAC while old binding is fresh ->
  reject and count conflict
- same GRUT address + different MAC after old binding is stale ->
  controlled rebind allowed
- table-full behavior deterministic; do not silently evict existing
  entries

### Exit criteria

Two real AIR nodes can maintain two stable address<->MAC bindings
simultaneously and independently age/recover.

### Evidence boundary

**Hardware-verified:** multiple distinct bindings coexisting,
independent aging, and same-address/same-MAC refresh (policy cases 1
and 2).

**Host/behavioral-test only, not yet hardware-exercised:** the
fresh-same-address/different-MAC rejection case and the
stale-same-address/different-MAC rebind case (policy cases 3 and 4).
Do not treat every conflict-policy branch as hardware-verified — see
`docs/ADR/0009-endpoint-conflict-policy-v1.md` for the exact split.

---

### Stage 5.1 — RouteTable core

**Status:** IMPLEMENTED + BUILD-TESTED

### Goal

Create a carrier-independent routing table:

```text
destination -> nextHop
```

Minimal entry:

```cpp
struct RouteEntry {
    uint8_t destination;
    uint8_t nextHop;
    uint8_t hopCount;
    uint32_t lastUpdatedMs;
    bool valid;
};
```

### Route policy v1

- first route inserts
- same next hop refreshes/updates
- lower hop count may replace higher hop count while the existing
  route is fresh
- higher-or-equal hop count from a different next hop does not
  replace an existing fresh route (no flapping)
- a stale existing route may be replaced regardless of hop count

### Status detail

Host tests pass (isolated RouteTable behavior). All four ESP targets
(`esp8285-air1`, `esp8285-air2`, `esp8285-ground`,
`esp8285-ground-linkdiag`) build successfully. Not yet
HARDWARE-VERIFIED — no measurement on real hardware has exercised
RouteTable's own behavior specifically (see Stage 5.2 below, which
*is* the runtime wiring and has its own separate verification need).

---

### Stage 5.2 — Direct routes in runtime

**Status:** HARDWARE-VERIFIED

### Goal

Translate fresh directly observed neighbors into direct routes:

```text
destination = neighbor
nextHop     = neighbor
hopCount    = 1
```

### Status detail

Wired into `bridge_main.cpp` and `link_diag_main.cpp`: every directly
heard neighbor now creates/refreshes a direct route via
`RouteTable::upsert()`. `link_diag_main.cpp` exposes a `ROUTE` line for
diagnostic visibility, following the same pattern as `NEIGHBOR` and
`BINDING`.

Hardware-verified on real AIR1+AIR2+GROUND hardware (GitHub Issue #1):

- both direct routes (`ROUTE dest=1 nextHop=1 hop=1` and
  `ROUTE dest=3 nextHop=3 hop=1`) observed simultaneously
- `age` refreshes independently per route
- powering AIR2 off: AIR1's route remains stable; AIR2's route ages
  without being evicted (no silent removal)
- powering AIR2 back on: its route refreshes correctly, with the
  endpoint table (ADR 0009) showing `conflicts=0`/`rebinds=0` for the
  same-MAC recovery
- no forwarding, relay, or mesh behavior was introduced or exercised

This verification covers only the direct-route case that the current
runtime wiring can produce (`destination == nextHop`, `hopCount == 1`).
`RouteTable`'s competing-next-hop policy (Stage 5.1's cases 3–5 — see
`docs/ADR/0010-route-policy-v1.md`) has no runtime producer yet and
remains BUILD-TESTED only; do not treat this Stage 5.2 hardware result
as verifying those cases too.

### Exit criteria

- direct routes appear/disappear or become stale deterministically
  — **HARDWARE-VERIFIED** (Issue #1)
- diagnostic firmware can show them — **done** (`ROUTE` line)
- routing still does not forward foreign frames — **true by
  construction; no forwarding code exists**
- existing production UART<->ESP-NOW bridge remains unaffected —
  **BUILD-TESTED** by regression/build evidence (Stage 5.2's own
  native tests and all four ESP builds). Issue #1 used
  `esp8285-ground-linkdiag`, which explicitly does not run the
  UART<->ESP-NOW bridge — a separate Mission Planner/production-bridge
  hardware regression check was not part of Issue #1 and remains
  outstanding.

---

## Stage 6 — Relay

**Status:** NOT YET

### Goal

Forward a GRUT frame whose final destination is another node via a
selected next hop.

### Required design work before code

GRUT wire protocol may need explicit forwarding semantics such as
destination, next-hop-independent addressing, TTL/hop limit, and
authenticated routing metadata.

Any wire-format change requires:

- compatibility analysis
- protocol version impact analysis
- `docs/PROTOCOL.md` update
- ADR if architecture changes

### Exit criteria

A controlled three-node test demonstrates:

```text
GROUND -> AIR1 -> AIR2
```

without UART corruption, uncontrolled loops, or accidental command
delivery to the relay node.

---

## Stage 7 — Mesh

**Status:** NOT YET

### Goal

Dynamic multi-hop behavior after direct routing and relay are already
stable and measurable.

### Explicit non-goals before this stage

Do not use mesh complexity to solve:

- point-to-point bridge issues
- neighbor discovery bugs
- RF hardware problems
- Mission Planner multi-vehicle UI problems

### Exit criteria

- neighbor changes update routes predictably
- route expiry/recovery is observable
- loops are bounded/prevented
- control, telemetry, management, and future high-bandwidth traffic
  classes are distinguished as needed

---

## Stage 8 — GRUT Desktop / multi-vehicle control

**Status:** NOT YET

### Goal

Expose multiple vehicles to the operator without forcing GRUT
Transport to parse MAVLink.

Possible architecture:

```text
AIR1 ----\
          GROUND <-> GRUT Desktop/router <-> logical GCS links
AIR2 ----/
```

Each ArduPilot vehicle must use a unique MAVLink `SYSID_THISMAV`.

Prefer separate logical streams/ports rather than byte-interleaving
multiple MAVLink streams into one raw UART stream.

### First autonomous multi-drone field milestone

A safe sequential test may be attempted only after addressable control
is proven:

```text
DRONE1: arm -> AUTO mission -> land -> disarm confirmed
                                      |
                                      v
DRONE2: arm -> AUTO same/assigned mission -> land -> disarm
```

This must not be triggered by a blind timer. The transition requires
confirmed vehicle state and an operator-abort path.

# 3. Hardware test milestones

## H0 — bench, no propellers

- two FC/AIR units connected
- send a command to AIR1: only FC1 reacts
- send a command to AIR2: only FC2 reacts
- telemetry remains isolated by vehicle/channel

## H1 — one flying vehicle

- normal AUTO mission through GRUT
- land/disarm behavior verified
- failsafe behavior verified independently

## H2 — two vehicles, sequential

- DRONE1 performs mission and fully lands/disarms
- system explicitly selects/addresses DRONE2
- DRONE2 performs mission
- human operator retains abort authority

## H3 — relay/multi-hop field test

Only after Stage 6 is hardware verified.

# 4. Rule for adding new roadmap items

A new feature enters the roadmap only if its responsible layer and
exit criteria are explicit.

Do not hide architecture changes inside implementation tasks.
