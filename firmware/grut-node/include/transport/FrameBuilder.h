#pragma once

#include <cstdint>

#include "transport/EspNowDriver.h"
#include "transport/UartTransport.h"

namespace grut {
namespace transport {

// UART -> GRUT DATA frames -> EspNowDriver (ADR 0005 pipeline, second
// box: "Frame Builder").
//
// Treats UART as an opaque byte stream (ADR 0005): does not parse
// MAVLink or align frames to message boundaries. Every poll() call
// drains whatever bytes are currently available from UartTransport,
// chunks them into frames of at most kChunkPayloadBytes each, and
// flushes immediately once no more bytes are available - this favors
// low latency over filling frames to the maximum, since MAVLink
// messages (e.g. heartbeats) should not sit buffered waiting for more
// traffic that may not arrive.
class FrameBuilder {
 public:
  static constexpr size_t kChunkPayloadBytes = 200;

  // srcAddr/dstAddr: this node's and its peer's GRUT protocol address
  // (see docs/PROTOCOL.md) - not MAC addresses.
  FrameBuilder(UartTransport& uart, EspNowDriver& espNow, uint8_t srcAddr,
               uint8_t dstAddr);

  // Drains available UART bytes and sends them as one or more GRUT
  // DATA frames. Call every loop() iteration while both uart and
  // espNow are running.
  void poll();

 private:
  void flush();

  UartTransport& uart_;
  EspNowDriver& espNow_;
  uint8_t srcAddr_;
  uint8_t dstAddr_;
  uint16_t nextSequence_ = 0;

  uint8_t chunkBuffer_[kChunkPayloadBytes];
  size_t chunkLength_ = 0;
};

}  // namespace transport
}  // namespace grut
