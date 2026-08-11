#pragma once

#include <cstddef>
#include <cstdint>

namespace grut {
namespace neighbor {

// NeighborTable: in-RAM knowledge of directly observed neighbors.
//
// Deliberately separate from link::LinkManager (link health only, one
// fixed peer) and from any future Routing/Relay/Mesh layer (not yet
// implemented - see docs/ADR/0007-link-manager-v1.md's roadmap notes).
// This class only records "what have I directly heard, and when" - it
// has no opinion on link health, recovery, or what to do with that
// information.
//
// Keyed by GRUT protocol address (GrutFrameHeader::srcAddr), not MAC:
// FrameReceiver's observer callbacks only expose the GRUT address, not
// the underlying ESP-NOW MAC (that lives one layer down, in
// EspNowDriver, and is not currently threaded up through the observer
// interface). Exposing MAC here would require a separate,
// larger interface change and is out of scope for this minimal core.
//
// No dynamic allocation: a fixed-size array sized for a small future
// neighbor count, not just today's single fixed peer - this table is
// meant to outlive the current two-node topology.
struct NeighborInfo {
  uint8_t address = 0;
  bool known = false;
  uint32_t lastSeenMs = 0;
  uint32_t rxFrames = 0;
  uint32_t sequenceGaps = 0;
};

class NeighborTable {
 public:
  static constexpr size_t kMaxNeighbors = 8;

  // A neighbor is considered fresh only if it was last observed within
  // this many milliseconds of "now" - see the class comment on why
  // staleness must be explicit (a hardware test showed a peer's
  // self-reported state can be many seconds stale and must not be
  // trusted as current).
  static constexpr uint32_t kDefaultStaleAfterMs = 5000;

  NeighborTable();

  // Records that a valid GRUT frame was received from `address` at
  // `nowMs`. `gapCount` is the number of apparently-missing frames
  // associated with this observation (0 for none) - callers typically
  // pass whatever their sequence-gap accounting already computed for
  // this frame (e.g. link::LinkManager's own gap logic), so this table
  // does not duplicate that math itself.
  //
  // If `address` is not yet known and the table is full
  // (count() == kMaxNeighbors), the observation is dropped and
  // droppedNewNeighborCount() is incremented - existing neighbors are
  // never evicted to make room. This is a deliberate simplification
  // for v1: with today's fixed two-node topology this can never
  // actually happen, and a real eviction policy needs its own design
  // once multi-neighbor topologies are in scope.
  void onFrameObserved(uint8_t address, uint32_t nowMs,
                       uint32_t gapCount = 0);

  // Returns a snapshot of what's known about `address`, or a
  // default-constructed (known == false) NeighborInfo if this address
  // has never been observed.
  NeighborInfo get(uint8_t address) const;

  // True only if `address` is known AND was last observed within
  // `staleAfterMs` of `nowMs`. Never treat a neighbor as usable without
  // checking this first - see the class comment.
  bool isFresh(uint8_t address, uint32_t nowMs,
               uint32_t staleAfterMs = kDefaultStaleAfterMs) const;

  size_t count() const;
  uint32_t droppedNewNeighborCount() const;

  void reset();

 private:
  int findIndex(uint8_t address) const;

  NeighborInfo entries_[kMaxNeighbors];
  size_t count_ = 0;
  uint32_t droppedNewNeighbors_ = 0;
};

}  // namespace neighbor
}  // namespace grut
