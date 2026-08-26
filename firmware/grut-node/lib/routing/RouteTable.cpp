#include "RouteTable.h"

namespace grut {
namespace routing {

RouteTable::RouteTable() {
  reset();
}

int RouteTable::findIndex(uint8_t destination) const {
  for (size_t i = 0; i < count_; ++i) {
    if (entries_[i].valid && entries_[i].destination == destination) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

bool RouteTable::upsert(uint8_t destination, uint8_t nextHop,
                        uint8_t hopCount, uint32_t nowMs,
                        uint32_t staleAfterMs) {
  const int idx = findIndex(destination);

  if (idx < 0) {
    // Case 1: no existing route.
    if (count_ >= kMaxRoutes) {
      ++droppedNewRoutes_;
      return false;
    }
    RouteEntry& entry = entries_[count_];
    entry.destination = destination;
    entry.nextHop = nextHop;
    entry.hopCount = hopCount;
    entry.lastUpdatedMs = nowMs;
    entry.valid = true;
    ++count_;
    return true;
  }

  RouteEntry& entry = entries_[idx];

  if (entry.nextHop == nextHop) {
    // Case 2: same next-hop re-reporting - refresh unconditionally,
    // not a competing route.
    entry.hopCount = hopCount;
    entry.lastUpdatedMs = nowMs;
    return true;
  }

  // Different next-hop competing for the same destination.
  const uint32_t age = nowMs - entry.lastUpdatedMs;
  const bool existingIsStale = age > staleAfterMs;

  if (existingIsStale) {
    // Case 5: freshness overrides the hop-count preference once the
    // old route can no longer be trusted.
    entry.nextHop = nextHop;
    entry.hopCount = hopCount;
    entry.lastUpdatedMs = nowMs;
    return true;
  }

  if (hopCount < entry.hopCount) {
    // Case 3: strictly better path.
    entry.nextHop = nextHop;
    entry.hopCount = hopCount;
    entry.lastUpdatedMs = nowMs;
    return true;
  }

  // Case 4: equal or worse hop count from a different next-hop while
  // the existing route is still fresh - keep it, do not flap.
  ++rejectedUpserts_;
  return false;
}

bool RouteTable::lookup(uint8_t destination, RouteEntry* out) const {
  const int idx = findIndex(destination);
  if (idx < 0) {
    return false;
  }
  *out = entries_[idx];
  return true;
}

bool RouteTable::remove(uint8_t destination) {
  const int idx = findIndex(destination);
  if (idx < 0) {
    return false;
  }
  for (size_t i = static_cast<size_t>(idx); i + 1 < count_; ++i) {
    entries_[i] = entries_[i + 1];
  }
  --count_;
  entries_[count_] = RouteEntry{};
  return true;
}

bool RouteTable::isFresh(uint8_t destination, uint32_t nowMs,
                         uint32_t staleAfterMs) const {
  const int idx = findIndex(destination);
  if (idx < 0) {
    return false;
  }
  const uint32_t age = nowMs - entries_[idx].lastUpdatedMs;
  return age <= staleAfterMs;
}

size_t RouteTable::count() const {
  return count_;
}

RouteEntry RouteTable::getByIndex(size_t index) const {
  if (index >= count_) {
    return RouteEntry{};
  }
  return entries_[index];
}

uint32_t RouteTable::droppedNewRouteCount() const {
  return droppedNewRoutes_;
}

uint32_t RouteTable::rejectedUpsertCount() const {
  return rejectedUpserts_;
}

void RouteTable::reset() {
  for (size_t i = 0; i < kMaxRoutes; ++i) {
    entries_[i] = RouteEntry{};
  }
  count_ = 0;
  droppedNewRoutes_ = 0;
  rejectedUpserts_ = 0;
}

}  // namespace routing
}  // namespace grut
