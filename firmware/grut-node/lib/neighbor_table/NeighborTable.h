#pragma once

#include <cstddef>
#include <cstdint>

namespace grut {
namespace neighbor {

// NeighborTable: in-RAM knowledge of directly observed neighbors.
//
// Deliberately separate from link::LinkManager (link health only, one
// fixed peer) and from any future Routing/Relay/Mesh layer. This class
// only records "what have I directly heard, and when" - it has no
// opinion on link health, recovery, or what to do with that
// information.
//
// Keyed by GRUT protocol address (GrutFrameHeader::srcAddr), not MAC:
// FrameReceiver's observer callbacks only expose the GRUT address, not
// the underlying ESP-NOW MAC.
//
// No dynamic allocation: a fixed-size array sized for a small future
// neighbor count, not just today's single fixed peer.
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
  // this many milliseconds of "now".
  static constexpr uint32_t kDefaultStaleAfterMs = 5000;

  NeighborTable();

  // Records that a valid GRUT frame was received from `address` at
  // `nowMs`. `gapCount` is the number of apparently-missing frames
  // associated with this observation (0 for none).
  //
  // If `address` is not yet known and the table is full
  // (count() == kMaxNeighbors), the observation is dropped and
  // droppedNewNeighborCount() is incremented - existing neighbors are
  // never evicted to make room.
  void onFrameObserved(uint8_t address, uint32_t nowMs,
                       uint32_t gapCount = 0);

  // Returns a snapshot of what's known about `address`, or a
  // default-constructed (known == false) NeighborInfo if this address
  // has never been observed.
  NeighborInfo get(uint8_t address) const;

  // True only if `address` is known AND was last observed within
  // `staleAfterMs` of `nowMs`.
  bool isFresh(uint8_t address, uint32_t nowMs,
               uint32_t staleAfterMs = kDefaultStaleAfterMs) const;

  size_t count() const;
  uint32_t droppedNewNeighborCount() const;

  // Enumeration support: valid indices are [0, count()). Returns a
  // default-constructed (known == false) NeighborInfo for any
  // out-of-range index. Entries are in insertion order (first-observed
  // first), stable - existing entries are never reordered or evicted.
  // Re-added for Stage 4.2's linkdiag visibility: showing more than
  // one discovered neighbor requires enumeration, not just get(addr)
  // for one already-known address.
  NeighborInfo getByIndex(size_t index) const;

  void reset();

 private:
  int findIndex(uint8_t address) const;

  NeighborInfo entries_[kMaxNeighbors];
  size_t count_ = 0;
  uint32_t droppedNewNeighbors_ = 0;
};

}  // namespace neighbor
}  // namespace grut
