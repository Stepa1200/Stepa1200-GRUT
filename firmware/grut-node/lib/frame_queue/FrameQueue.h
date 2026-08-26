#pragma once

#include <cstddef>
#include <cstdint>

namespace grut {
namespace transport {

// Fixed-depth ring buffer of bounded byte frames, with drop-newest
// behavior on overflow (ADR 0006): if the queue is full when push() is
// called, the new frame is rejected and a counter is incremented -
// frames already queued are never evicted.
//
// No Arduino dependency. Used by EspNowDriver on both the send and
// receive side; independently unit-tested (see test/test_frame_queue).
class FrameQueue {
 public:
  static constexpr size_t kDepth = 4;
  static constexpr size_t kMaxFrameBytes = 250;  // grut::protocol::kMaxFrameSizeBytes
  static constexpr size_t kMacBytes = 6;

  FrameQueue() = default;

  // Returns false (and increments droppedCount()) if the queue is
  // already full or frameLength exceeds kMaxFrameBytes. Existing
  // queued frames are untouched either way.
  //
  // mac: optional, 6 bytes, stored alongside the frame if provided
  // (nullptr means "not tracked" - existing callers are unaffected,
  // this parameter defaults to nullptr precisely so no call site needs
  // to change). Added for Stage 5.0 (multi-peer endpoint plumbing) so
  // EspNowDriver's receive queue can preserve the ESP-NOW source MAC
  // through to FrameReceiver, without FrameQueue itself knowing or
  // caring what that MAC is used for.
  bool push(const uint8_t* frameBytes, size_t frameLength,
            const uint8_t* mac = nullptr);

  // Returns false if the queue is empty. On success, copies the
  // oldest queued frame's bytes into outBuffer (capacity outCapacity,
  // must be at least kMaxFrameBytes) and sets *outLength, then removes
  // it from the queue (FIFO order).
  //
  // outMac: optional, 6 bytes, filled with whatever was passed to the
  // matching push() (all zero if push() didn't provide one). nullptr
  // means "caller doesn't care" - existing callers are unaffected.
  bool pop(uint8_t* outBuffer, size_t outCapacity, size_t* outLength,
           uint8_t* outMac = nullptr);

  size_t size() const;
  bool empty() const;
  bool full() const;
  uint32_t droppedCount() const;

 private:
  struct Slot {
    uint8_t bytes[kMaxFrameBytes] = {};
    size_t length = 0;
    uint8_t mac[kMacBytes] = {};
  };

  Slot slots_[kDepth];
  size_t head_ = 0;  // index of the next slot to pop
  size_t count_ = 0;
  uint32_t dropped_ = 0;
};

}  // namespace transport
}  // namespace grut
