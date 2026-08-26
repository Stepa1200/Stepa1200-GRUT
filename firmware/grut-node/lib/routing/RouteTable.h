#pragma once

#include <cstddef>
#include <cstdint>

namespace grut {
namespace routing {

// RouteTable: destination -> next-hop knowledge, for the future
// Networking layer (ADR roadmap: LinkManager -> NeighborTable ->
// RouteTable -> Routing/Relay/Mesh).
//
// This is a deliberately isolated, purely algorithmic component
// (Stage 5.1): it does not forward packets, does not know ESP-NOW
// MACs, does not parse MAVLink, and is not wired into NeighborTable or
// any transport code in this stage. It answers exactly one question:
// "for destination D, which directly-reachable GRUT next-hop should be
// used?" - nothing more.
//
// Deliberately mirrors NeighborTable's shape (fixed-size array,
// insertion-order/no-eviction, get/isFresh/count/getByIndex/reset) for
// consistency across the three "which node" tables in this codebase
// (NeighborTable: who's directly heard; EspNowDriver's peer bindings:
// which MAC for a direct next-hop; RouteTable: which next-hop for a
// destination).
struct RouteEntry {
  uint8_t destination = 0;
  uint8_t nextHop = 0;
  uint8_t hopCount = 0;
  uint32_t lastUpdatedMs = 0;
  bool valid = false;
};

class RouteTable {
 public:
  static constexpr size_t kMaxRoutes = 8;

  // Same horizon as NeighborTable::kDefaultStaleAfterMs and
  // EspNowDriver::kPeerBindingStaleAfterMs - all three answer the same
  // underlying question ("how long since we last heard something
  // before we stop trusting it") for different data. Also used
  // internally by upsert() to decide whether an existing route may be
  // replaced by a different, not-strictly-better next-hop (policy
  // rule 5 below).
  static constexpr uint32_t kDefaultStaleAfterMs = 5000;

  RouteTable();

  // Route policy v1 - deterministic, never flaps on equal information:
  //  1. No existing route for `destination`       -> route created.
  //  2. Existing route, SAME nextHop               -> refreshed
  //     (hopCount and lastUpdatedMs updated unconditionally - this is
  //     the same next-hop simply re-reporting, not a competing route).
  //  3. Existing route, DIFFERENT nextHop,
  //     existing route still fresh, new hopCount
  //     is LOWER                                   -> replaced.
  //  4. Existing route, DIFFERENT nextHop,
  //     existing route still fresh, new hopCount
  //     is EQUAL or HIGHER                          -> kept as-is
  //     (explicitly does not flap between
  //     equally-good alternate paths).
  //  5. Existing route, DIFFERENT nextHop,
  //     existing route is STALE (age > staleAfterMs) -> replaced
  //     regardless of hopCount (freshness overrides the hop-count
  //     preference once the old information can no longer be trusted).
  // If the table is full and this is a genuinely new destination, the
  // call is rejected (droppedNewRouteCount() incremented) - existing
  // routes are never evicted to make room.
  //
  // Returns true if the table was actually created/refreshed/replaced,
  // false if policy rejected the update (case 4) or the table was full
  // (case 1 variant). rejectedUpsertCount() tracks case 4 specifically,
  // separate from droppedNewRouteCount()'s table-full case.
  bool upsert(uint8_t destination, uint8_t nextHop, uint8_t hopCount,
              uint32_t nowMs,
              uint32_t staleAfterMs = kDefaultStaleAfterMs);

  // Returns true and fills *out if a route exists for `destination`,
  // regardless of freshness (matching NeighborTable::get()'s own
  // convention - freshness is a separate, explicit query).
  bool lookup(uint8_t destination, RouteEntry* out) const;

  // Removes the route for `destination`, if any. Returns true if a
  // route was actually removed. Compacts the table (later entries
  // shift down) to keep getByIndex() enumeration gap-free, matching
  // count() staying an exact, dense [0, count()) range.
  bool remove(uint8_t destination);

  bool isFresh(uint8_t destination, uint32_t nowMs,
               uint32_t staleAfterMs = kDefaultStaleAfterMs) const;

  size_t count() const;
  RouteEntry getByIndex(size_t index) const;

  uint32_t droppedNewRouteCount() const;
  uint32_t rejectedUpsertCount() const;

  void reset();

 private:
  int findIndex(uint8_t destination) const;

  RouteEntry entries_[kMaxRoutes];
  size_t count_ = 0;
  uint32_t droppedNewRoutes_ = 0;
  uint32_t rejectedUpserts_ = 0;
};

}  // namespace routing
}  // namespace grut
