#pragma once

#include "transport/EspNowDriver.h"
#include "transport/UartTransport.h"

namespace grut {
namespace transport {

// EspNowDriver -> decoded GRUT DATA frames -> UART (ADR 0005 pipeline,
// fourth box: "Frame Receiver").
//
// Drains any frames waiting in EspNowDriver's receive queue, decodes
// each via FrameCodec, and writes kData payload bytes out to UART in
// the order received. Malformed/corrupted frames (bad CRC, wrong
// version, etc.) and non-kData frame types (heartbeat/control -
// reserved, not implemented yet) are silently dropped - consistent
// with ADR 0005's "no lossless delivery guarantee" for v0.2.0.
class FrameReceiver {
 public:
  FrameReceiver(EspNowDriver& espNow, UartTransport& uart);

  // Call every loop() iteration while both espNow and uart are
  // running.
  void poll();

 private:
  EspNowDriver& espNow_;
  UartTransport& uart_;
};

}  // namespace transport
}  // namespace grut
