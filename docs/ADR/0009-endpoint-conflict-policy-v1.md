# 0009 — Endpoint (GRUT Address ↔ ESP-NOW MAC) Conflict Policy v1

## Status

Accepted. IMPLEMENTED + BUILD-TESTED for all four policy cases.
Cases 1 and 2 (new binding, same-MAC refresh) are additionally
HARDWARE-VERIFIED, as is the stale/no-eviction/independent-aging
behavior of the table as a whole. Cases 3 and 4 (fresh-conflict
rejection, stale rebind onto a genuinely different MAC) have not been
exercised on real hardware — see Compatibility impact for the exact
split.

## Context

Stage 4.2 gave GROUND the ability to discover multiple GRUT addresses
(via HELLO/ADR 0008). A future Routing layer will need to translate a
GRUT next-hop address into an actual ESP-NOW transmission — but the
existing `EspNowDriver` (ADR 0006) only ever knew one fixed,
constructor-supplied `peerMac_`, and its `onReceive()` callback
discarded the ESP-NOW source MAC entirely.

Two design questions had to be answered before any such mapping could
exist:

1. **Who owns the GRUT-address → MAC mapping?** `NeighborTable` (ADR-
   documented as carrier-independent, keyed only on GRUT address) was
   an obvious place to be tempted to bolt this on, but doing so would
   make it ESP-NOW-specific and routing-adjacent — exactly what it is
   designed not to be.
2. **What happens if the same GRUT address is suddenly heard from a
   different MAC?** This is not a hypothetical — it is a real
   scenario (a node's radio module replaced, a duplicate/misconfigured
   node, or a genuine addressing mistake) with actual delivery-safety
   consequences: a wrong or hijacked binding means `sendToPeer()`
   would confidently transmit into the wrong physical device.

## Decision

### Ownership: Transport, not NeighborTable

The binding table lives inside `EspNowDriver` (`PeerBinding` array,
`kMaxPeerBindings = 8`, fixed-size, no dynamic allocation) —
Transport already owns MAC addresses and peer registration (ADR 0006);
this is a natural extension of that existing responsibility, not a new
one. `NeighborTable` is untouched by this work and remains exactly as
carrier-independent as before.

`FrameReceiver` is the only component that has both pieces of
information available at the same moment: it decodes the GRUT header
(learning `srcAddr`) immediately after calling
`EspNowDriver::receive()`, which was extended with an optional output
parameter for the ESP-NOW source MAC:

```cpp
bool receive(uint8_t* outBuffer, size_t outCapacity, size_t* outLength,
             uint8_t* outMac = nullptr);
```

(Backward compatible: existing callers passing three arguments are
unaffected, since `outMac` defaults to `nullptr`.) The underlying
`FrameQueue::push()`/`pop()` gained the identical optional-MAC pattern
so the receive queue preserves the MAC in lockstep with the frame
bytes, without changing behavior for existing callers — though this
does add fixed RAM overhead, since MAC storage is now part of every
`FrameQueue::Slot`, including `sendQueue_`'s slots, which never
populate it. `EspNowDriver` holds two `FrameQueue` instances
(`sendQueue_`, `recvQueue_`), so this overhead applies to both; the
exact aligned `sizeof` impact has not been measured and is not stated
here as a number.

`FrameReceiver` does not store or interpret the binding — it only
reports it, once per successfully decoded frame of any type, via a new
`EspNowDriver` method:

```cpp
void recordPeerBinding(uint8_t grutAddr, const uint8_t* mac);
```

This keeps `FrameReceiver`'s own responsibility unchanged in spirit
(decode and dispatch); it does not decide policy, it forwards a fact.

### Conflict policy v1 — deterministic, not last-seen-wins

Unconditional "last-seen-wins" (any new MAC for a known address
immediately replacing the binding) was rejected in favor of the
following, on the basis that a stale MAC binding is not merely
cosmetic the way a stale `NeighborTable` visibility entry is — it
actively breaks future delivery, silently sending frames to the wrong
device:

1. **New GRUT address + MAC** → binding created. `esp_now_add_peer()`
   is called immediately so a later `sendToPeer()` can reach it
   without a separate registration step.
2. **Existing address + the SAME MAC** → `lastSeenMs` refreshed only.
   This is the common case (the same node's own periodic traffic).
3. **Existing address + a DIFFERENT MAC, while the current binding is
   still fresh** (age ≤ `kPeerBindingStaleAfterMs`, currently 5000ms —
   the same horizon as `NeighborTable::kDefaultStaleAfterMs`, chosen
   for consistency since both answer "how long since we last heard
   from this address before we stop trusting what we knew") →
   **rejected**. The existing binding is left completely untouched;
   `addressMacConflictCount()` is incremented. A fresh binding is
   never silently overwritten by a second MAC claiming the same
   address.
4. **Existing address + a DIFFERENT MAC, after the current binding has
   gone stale** → rebind allowed. The old MAC is deregistered
   (`esp_now_del_peer()`), the new one registered, and
   `rebindCount()` incremented.

If the table is full and the address is genuinely new,
`droppedNewBindingCount()` is incremented and nothing is recorded —
existing bindings are never evicted to make room, mirroring
`NeighborTable`'s own no-eviction stance.

This is explicitly **not** authentication or security. It is
deterministic endpoint bookkeeping ahead of a future SecurityManager;
it defends against accidental/ambiguous rebinding, not against a
deliberately malicious actor forging GRUT addresses.

## Consequences

- `sendToPeer(nextHopAddr, frame, length)` exists as a raw capability
  (best-effort, no queuing/retry, consistent with ADR 0005's "no
  lossless delivery guarantee" philosophy) for a future Routing layer
  to call. No routing decision logic exists yet — nothing currently
  calls it with a computed next hop.
- Diagnostic visibility (`esp8285-ground-linkdiag`'s `BINDING addr=...
  mac=... age=...` and `BINDING_STATS count=... dropped=... conflicts=...
  rebinds=...` lines) was added specifically so this policy's behavior
  is observable on real hardware, not just inferred from counters in a
  test.
- The conflict/rebind thresholds (`kPeerBindingStaleAfterMs = 5000`)
  are a first choice, not derived from a specific measurement — same
  caveat as HELLO's interval in ADR 0008.

## Alternatives considered

- **Last-seen-wins (unconditional rebind).** Rejected: it would let a
  second MAC silently replace a still-fresh binding for the same GRUT
  address, with real delivery-safety consequences (see Context above).
- **Storing the MAC inside `NeighborTable`.** Rejected — would violate
  the explicit "NeighborTable stays carrier-independent" boundary and
  create a second place (alongside `EspNowDriver`) that could disagree
  about what MAC an address maps to.
- **A separate parallel array for received MACs alongside `FrameQueue`**,
  instead of extending `FrameQueue` itself. A hand-maintained parallel
  ring buffer risks desynchronizing from the main queue's head/count
  bookkeeping; extending `FrameQueue::Slot` directly guarantees
  lockstep by construction, reusing the existing, already-tested
  push/pop logic, at the cost of a few unused bytes per slot on the
  send-side queue (see Decision above).

## Compatibility impact

**NONE to the GRUT wire format.** The MAC never appears in
`GrutFrameHeader` or anywhere in `FrameCodec` — it is local RAM
bookkeeping passed between `EspNowDriver` and `FrameReceiver` purely
via function parameters, never serialized.

`FrameQueue::push()`/`pop()`'s new MAC parameter defaults to `nullptr`
in both places — every pre-existing call site is unaffected, and
`FrameQueue`'s own existing native tests continue to pass unmodified.

Native test coverage: the Stage 5.0 implementation commit (`0fc8d67`)
records 74/74 official native suites plus 22 new behavioral tests via
the project's ESP8266-mock harness (`EspNowDriver` requires
Arduino/ESP-NOW mocks and is outside the plain `pio test -e native`
suite for that reason). These 22 tests cover MAC preservation through
the RX queue, independent bindings for two addresses,
refresh-not-duplicate on repeat, the fresh-conflict-rejected case,
rebind-after-stale (simulated time), and deterministic table-full
behavior — plus a Stage 4.2 safety-gate regression check (still
passing, no DATA-stream merging introduced by this work).

Hardware-verified (GRUT commit `0fc8d67` for the Stage 5.0
implementation itself; commit `505ce6b` for the `BINDING` linkdiag
diagnostic line that made this observable): GROUND simultaneously
maintained two real bindings (`addr=1 -> AIR1 MAC`, `addr=3 -> AIR2
MAC`) with independently aging `age` fields; powering AIR2 off left
its binding in place (aging, not evicted) while AIR1 remained stable;
powering AIR2 back on with the same MAC refreshed the binding with
`conflicts=0` and `rebinds=0`, matching policy case 2. **Not**
hardware-tested: a fresh binding being challenged by a genuinely
different MAC (case 3), and a stale binding accepting a rebind onto a
different MAC (case 4) — both are host-test-proven only (see the
behavioral test suite above), not yet reproduced on real hardware.
