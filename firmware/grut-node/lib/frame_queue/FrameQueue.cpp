#include "FrameQueue.h"

#include <cstring>

namespace grut {
namespace transport {

bool FrameQueue::push(const uint8_t* frameBytes, size_t frameLength) {
  if (count_ >= kDepth || frameLength > kMaxFrameBytes) {
    ++dropped_;
    return false;
  }

  const size_t tail = (head_ + count_) % kDepth;
  std::memcpy(slots_[tail].bytes, frameBytes, frameLength);
  slots_[tail].length = frameLength;
  ++count_;
  return true;
}

bool FrameQueue::pop(uint8_t* outBuffer, size_t outCapacity, size_t* outLength) {
  if (count_ == 0) {
    return false;
  }

  const Slot& slot = slots_[head_];
  if (slot.length > outCapacity) {
    // Should be unreachable given the kMaxFrameBytes bound enforced by
    // push(), but fail safe rather than overflow the caller's buffer.
    return false;
  }

  std::memcpy(outBuffer, slot.bytes, slot.length);
  *outLength = slot.length;

  head_ = (head_ + 1) % kDepth;
  --count_;
  return true;
}

size_t FrameQueue::size() const {
  return count_;
}

bool FrameQueue::empty() const {
  return count_ == 0;
}

bool FrameQueue::full() const {
  return count_ >= kDepth;
}

uint32_t FrameQueue::droppedCount() const {
  return dropped_;
}

}  // namespace transport
}  // namespace grut
