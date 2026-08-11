#pragma once

#include <cstdint>

#include "transport/EspNowDriver.h"
#include "transport/SequenceGenerator.h"
#include "transport/UartTransport.h"

namespace grut {
namespace transport {

// UART -> GRUT DATA frames -> EspNowDriver (ADR 0005 pipeline, second
// box: "Frame Builder").
//
// Treats UART as an opaque byte stream (ADR 0005): does not parse
// MAVLink or align frames to message boundaries. Bytes are aggregated
// into a chunk and flushed as one GRUT frame when either:
//   (a) the chunk reaches kChunkPayloadBytes, or
//   (b) no new byte has arrived for kIdleFlushMicros.
//
// (b) is deliberate and load-bearing: loop() runs far faster than
// bytes arrive at 57600 baud (~174us/byte), so flushing unconditionally
// whenever nothing is *immediately* available (the original v0.2.0
// design) fragments a single MAVLink packet into a storm of ~1-3 byte
// GRUT frames - each carrying 11 bytes of header+CRC overhead and
// consuming one EspNowDriver send-queue slot, overwhelming the queue
// long before real payload accumulates. Waiting for a short idle gap
// instead lets a full UART burst (typically a whole MAVLink packet, or
// several back-to-back) accumulate into one frame, at the cost of up
// to kIdleFlushMicros of added latency - negligible for telemetry.
class FrameBuilder {
 public:
  static constexpr size_t kChunkPayloadBytes = 200;
  static constexpr unsigned long kIdleFlushMicros = 2000;  // 2 ms

  // srcAddr/dstAddr: this node's and its peer's GRUT protocol address
  // (see docs/PROTOCOL.md) - not MAC addresses.
  // sequence: node-wide sequence allocator shared with every other
  // outbound GRUT frame producer (heartbeat/control included).
  FrameBuilder(UartTransport& uart, EspNowDriver& espNow,
               SequenceGenerator& sequence, uint8_t srcAddr,
               uint8_t dstAddr);

  // Drains available UART bytes into the current chunk and flushes per
  // the rule above. Call every loop() iteration while both uart and
  // espNow are running.
  void poll();

  // Diagnostic counters, monotonically increasing from construction.
  uint32_t uartBytesRead() const;
  uint32_t framesSent() const;

 private:
  void flush();

  UartTransport& uart_;
  EspNowDriver& espNow_;
  SequenceGenerator& sequence_;
  uint8_t srcAddr_;
  uint8_t dstAddr_;

  uint8_t chunkBuffer_[kChunkPayloadBytes];
  size_t chunkLength_ = 0;
  unsigned long lastByteMicros_ = 0;

  uint32_t uartBytesRead_ = 0;
  uint32_t framesSent_ = 0;
};

}  // namespace transport
}  // namespace grut
