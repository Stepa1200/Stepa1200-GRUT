# 0010 — RouteTable Policy v1

## Status

Accepted. RouteTable core (Stage 5.1) and its direct-route runtime
population (Stage 5.2) are IMPLEMENTED + BUILD-TESTED; Stage 5.2's
runtime wiring is additionally HARDWARE-VERIFIED (Issue #1). See
Compatibility impact for the exact evidence split.

## Context

Stage 5.0 (ADR 0009) gave Transport a way to reach a directly-visible
GRUT neighbor by MAC. Before any forwarding/relay/mesh behavior can
exist, GRUT needs a carrier-independent answer to a narrower question
first: for a given destination address, which next-hop address should
be used? Stage 5.1's explicit goal was to build this as an isolated,
purely algorithmic component — no forwarding, no relay, no route
selection integrated into any send path — precisely so its own
correctness could be established with host tests before it touches
anything live.

## Decision

### Ownership and shape

`RouteTable` (`lib/routing/`) deliberately mirrors `NeighborTable`'s
shape: fixed-size array (`kMaxRoutes = 8`, chosen to match
`NeighborTable::kMaxNeighbors` and `EspNowDriver::kMaxPeerBindings` for
consistency across the three "which node" tables in this codebase),
insertion-order with no eviction of existing entries, and the same
`lookup`/`isFresh`/`count`/`getByIndex`/`reset` shape already established
by `NeighborTable`.

```cpp
struct RouteEntry {
  uint8_t destination;
  uint8_t nextHop;
  uint8_t hopCount;
  uint32_t lastUpdatedMs;
  bool valid;
};
```

`RouteTable` does not know ESP-NOW MACs (that is
`EspNowDriver`/ADR 0009's job) and does not parse MAVLink. It answers
exactly one question — "for destination D, which next hop N should be
used?" — and nothing else. No RSSI-based metric is used anywhere in
this policy: ESP8266 ESP-NOW RSSI is not verified/available via the
official API this project uses (confirmed during the original
LinkManager work, ADR 0007), so `hopCount` is the only ranking signal.

### Route policy v1 — deterministic, never flaps

```cpp
bool upsert(uint8_t destination, uint8_t nextHop, uint8_t hopCount,
            uint32_t nowMs, uint32_t staleAfterMs = kDefaultStaleAfterMs);
```

1. **No existing route for `destination`** → route created.
2. **Existing route, the SAME `nextHop` reporting again** → refreshed
   unconditionally (`hopCount` and `lastUpdatedMs` updated) — this is
   not a competing route, just the same path being reconfirmed.
3. **Existing route, a DIFFERENT `nextHop`, existing route still
   fresh, new `hopCount` is LOWER** → replaced (a strictly better path
   was found).
4. **Existing route, a DIFFERENT `nextHop`, existing route still
   fresh, new `hopCount` is EQUAL or HIGHER** → **rejected**, kept
   as-is. This is the explicit anti-flap rule: two equally-good
   alternate paths must never cause the table to oscillate between
   them. `rejectedUpsertCount()` tracks this case.
5. **Existing route, a DIFFERENT `nextHop`, existing route is STALE**
   (age > `staleAfterMs`, default `kDefaultStaleAfterMs = 5000` — the
   same horizon used throughout this codebase's "which node" tables)
   → replaced regardless of hop count. Freshness overrides the
   hop-count preference once the old information can no longer be
   trusted; a stale route that happens to have a lower recorded hop
   count is not evidence of a better path right now.

Table-full behavior mirrors ADR 0009: a genuinely new destination is
rejected (`droppedNewRouteCount()`) when the table is full; existing
routes are never evicted. `remove()` compacts the array so
`getByIndex()` enumeration stays dense across `[0, count())`.

### Stage 5.2 — direct routes, wired at the application layer only

Every directly-heard neighbor is, by definition, one hop away. This is
populated by a single call in `bridge_main.cpp`'s (and, for
consistency, `link_diag_main.cpp`'s) frame-observation path, placed
immediately next to the existing `NeighborTable::onFrameObserved()`
call:

```cpp
gRouteTable.upsert(srcAddr, /*nextHop=*/srcAddr, /*hopCount=*/1, now);
```

This wiring lives at the application layer specifically so that
`NeighborTable` and `RouteTable` remain mutually unaware of each
other — the same principle ADR 0009 applied to `NeighborTable` and
`EspNowDriver`. Neither table imports or references the other; a third
party (the bridge/diagnostic firmware's own code) is the only thing
that knows both exist and connects them. No forwarding, relay, or
route-selection logic exists at this call site or anywhere else in the
codebase as of this stage — it only populates the table.

`esp8285-ground-linkdiag` exposes the resulting state via a new
`ROUTE dest=... nextHop=... hop=... age=...` line, one per entry,
following the same pattern already established for `NEIGHBOR` and
`BINDING`.

## Consequences

- The current runtime wiring (`gRouteTable.upsert(srcAddr,
  /*nextHop=*/srcAddr, /*hopCount=*/1, now)`) only ever exercises
  direct route creation/refresh, where `destination == nextHop` and
  `hopCount == 1` always. There is currently **no producer anywhere in
  the codebase** of a route whose destination arrives through a
  *different* next hop — no route advertisement, no multi-hop
  computation, nothing. Policy cases 3, 4, and 5 (competing next-hop
  selection, anti-flap, stale-override) are therefore exercised
  **only by host tests** calling `RouteTable::upsert()` directly with
  synthetic competing observations; they are not reachable through any
  current runtime code path. This distinction matters directly for
  Relay (Stage 6): those cases must not be assumed proven for real
  multi-hop traffic just because they are proven correct in isolation.
- `RouteTable` state has no effect on any current send/receive
  behavior. Reading it (`lookup()`, `getByIndex()`) is the only
  consumer today, via diagnostics. This is intentional: Stage 5.1/5.2
  exist to make the data structure and its population correct and
  observable *before* anything is allowed to act on it.
- The route policy's anti-flap rule (case 4) and stale-override rule
  (case 5) were written to model realistic future multi-hop
  competition even though no code path can currently produce it —
  this is forward preparation, consistent with `NeighborTable` and the
  endpoint-binding table both being sized and shaped for more than
  today's exact node count.

## Alternatives considered

- **Wiring direct routes directly into
  `NeighborTable::onFrameObserved()`** rather than as a separate call
  at the application layer. Rejected: this would make `NeighborTable`
  aware of routing concepts, conflicting with the explicit
  architectural rule that it must not be. The wiring instead lives at
  the application layer (`bridge_main.cpp`/`link_diag_main.cpp`, see
  "Stage 5.2" above) specifically so `NeighborTable` and `RouteTable`
  remain mutually unaware of each other.
- **Using RSSI as a tiebreaker or metric input.** Rejected outright —
  not available/verified on this hardware's ESP-NOW API (see ADR
  0007), and the task explicitly excluded inventing one.
- **Last-seen-wins for competing routes** (mirroring what was
  originally proposed and rejected for endpoint bindings in ADR 0009).
  Rejected for the same reason: it would let a worse, newer
  observation silently replace a better, still-fresh one, causing
  exactly the flapping case 4 is designed to prevent.

## Compatibility impact

**NONE to the GRUT wire format.** `RouteTable` is a purely local,
host-side data structure; nothing it stores is ever serialized onto
the wire. No `PacketType`, header field, or CRC/version behavior is
touched.

Native test coverage: 15 tests (`test_route_table`) covering insert,
lookup, refresh, lower/higher/equal hop-count competition (including
the explicit anti-flap case), stale-route replacement, remove with
array compaction, table-full determinism, duplicate-destination
non-duplication, and `millis()` rollover-safe freshness. All four ESP
targets (`esp8285-air1`, `esp8285-air2`, `esp8285-ground`,
`esp8285-ground-linkdiag`) build successfully with the Stage 5.2
wiring included — commit `505ce6b` (89/89 native tests overall, 15 of
them `RouteTable`'s own; all four builds SUCCESS). Commit `4263081` is
a documentation/workflow-adoption commit only and is not build/test
evidence for this ADR.

Hardware-verified (Issue #1, `esp8285-ground-linkdiag` on real
AIR1+AIR2+GROUND): `ROUTE dest=1 nextHop=1 hop=1` and
`ROUTE dest=3 nextHop=3 hop=1` observed simultaneously, `age` updating
independently per route; powering AIR2 off left its route in place,
aging without eviction, with no effect on AIR1's route or LinkManager
state; powering AIR2 back on with the same MAC refreshed its route
correctly with the endpoint table showing `conflicts=0`/`rebinds=0`
(ADR 0009 policy case 2), confirming the two tables' independent but
consistent behavior under the same real hardware event. This
hardware verification covers only the direct-route case (`destination
== nextHop`, `hopCount == 1`) exercised by the current runtime wiring
(Stage 5.2). The competing-next-hop policy cases (3–5 above) remain
IMPLEMENTED + BUILD-TESTED only — no hardware scenario has exercised
them, and none currently can, per the Consequences section above.
