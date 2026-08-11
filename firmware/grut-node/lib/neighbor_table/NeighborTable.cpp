#include "NeighborTable.h"

namespace grut {
namespace neighbor {

NeighborTable::NeighborTable() {
  reset();
}

int NeighborTable::findIndex(uint8_t address) const {
  for (size_t i = 0; i < count_; ++i) {
    if (entries_[i].known && entries_[i].address == address) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

void NeighborTable::onFrameObserved(uint8_t address, uint32_t nowMs,
                                    uint32_t gapCount) {
  const int existing = findIndex(address);
  if (existing >= 0) {
    NeighborInfo& info = entries_[existing];
    info.lastSeenMs = nowMs;
    ++info.rxFrames;
    info.sequenceGaps += gapCount;
    return;
  }

  if (count_ >= kMaxNeighbors) {
    ++droppedNewNeighbors_;
    return;
  }

  NeighborInfo& info = entries_[count_];
  info.address = address;
  info.known = true;
  info.lastSeenMs = nowMs;
  info.rxFrames = 1;
  info.sequenceGaps = gapCount;
  ++count_;
}

NeighborInfo NeighborTable::get(uint8_t address) const {
  const int idx = findIndex(address);
  if (idx < 0) {
    return NeighborInfo{};
  }
  return entries_[idx];
}

bool NeighborTable::isFresh(uint8_t address, uint32_t nowMs,
                            uint32_t staleAfterMs) const {
  const int idx = findIndex(address);
  if (idx < 0) {
    return false;
  }
  // Unsigned subtraction wraps correctly even across millis() rollover,
  // matching the pattern already used in link::LinkManager.
  const uint32_t age = nowMs - entries_[idx].lastSeenMs;
  return age <= staleAfterMs;
}

size_t NeighborTable::count() const {
  return count_;
}

uint32_t NeighborTable::droppedNewNeighborCount() const {
  return droppedNewNeighbors_;
}

void NeighborTable::reset() {
  for (size_t i = 0; i < kMaxNeighbors; ++i) {
    entries_[i] = NeighborInfo{};
  }
  count_ = 0;
  droppedNewNeighbors_ = 0;
}

}  // namespace neighbor
}  // namespace grut
