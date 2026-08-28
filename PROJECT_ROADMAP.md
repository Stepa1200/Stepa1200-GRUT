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
- discovery must not authorize DATA automatically.
- multiple MAVLink byte streams must never be merged into one UART
  stream.

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

**Status:** IMPLEMENTED + BUILD-TESTED, NOT HARDWARE-VERIFIED

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

A real behavioral test against the actual `link_diag_main.cpp` code
(simulating two distinct source addresses) confirmed both direct
routes appear simultaneously in the diagnostic output. This has **not**
been reproduced by flashing real hardware yet — that is the specific
remaining step before this can be called HARDWARE-VERIFIED.

### Exit criteria

- direct routes appear/disappear or become stale deterministically
  — **BUILD-TESTED, pending hardware confirmation**
- diagnostic firmware can show them — **done** (`ROUTE` line)
- routing still does not forward foreign frames — **true by
  construction; no forwarding code exists**
- existing direct bridge remains unaffected — **BUILD-TESTED**
  (regression suite + safety-gate check both pass); real-hardware
  regression check (Mission Planner quality) still recommended before
  calling this stage closed

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
