#pragma once

#include <cstdint>

namespace grut {
namespace transport {

// Allocates the per-sender GRUT frame sequence number.
//
// One instance must be shared by every producer of outbound GRUT frames
// on a node (DATA, HEARTBEAT, CONTROL, and future packet types). This keeps
// header.sequence authoritative for link-level loss accounting instead of
// maintaining independent counters per packet type.
//
// uint16_t wrap-around is intentional protocol behavior:
//   ... 65534, 65535, 0, 1 ...
class SequenceGenerator {
 public:
  explicit SequenceGenerator(uint16_t initialSequence = 0)
      : nextSequence_(initialSequence) {}

  uint16_t next() {
    const uint16_t value = nextSequence_;
    nextSequence_ = static_cast<uint16_t>(nextSequence_ + 1u);
    return value;
  }

 private:
  uint16_t nextSequence_;
};

}  // namespace transport
}  // namespace grut