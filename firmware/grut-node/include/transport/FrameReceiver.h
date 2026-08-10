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
// version, etc.) are silently dropped - consistent with ADR 0005's "no
// lossless delivery guarantee" for v0.2.0.
class FrameReceiver {
 public:
  // debugPrintHeartbeats: TEMPORARY bring-up aid only, defaults to
  // false (production behavior: heartbeat/control frames are silently
  // dropped, same as always). When explicitly set true, a received
  // kHeartbeat frame writes a short "HB\r\n" marker to UART - useful
  // to prove the ESP-NOW link is alive independent of any real UART
  // traffic (flight controller, MAVLink) while bringing up a new pair
  // of nodes. Must be turned back off once the link is proven, since
  // writing anything unsolicited to UART conflicts with "no
  // diagnostic text on the MAVLink line" once real traffic is flowing.
  //
  // debugPrintStats: TEMPORARY diagnostic aid, also defaults to
  // false. When true, a received kControl frame carrying this node's
  // ad hoc stats payload (see bridge_main.cpp's sendStats()) is
  // parsed and printed as one human-readable ASCII line to UART. Same
  // "must be off once real data is flowing" caveat as
  // debugPrintHeartbeats applies.
  explicit FrameReceiver(EspNowDriver& espNow, UartTransport& uart,
                          bool debugPrintHeartbeats = false,
                          bool debugPrintStats = false);

  // Call every loop() iteration while both espNow and uart are
  // running.
  void poll();

  // Diagnostic counters, all monotonically increasing from
  // construction.
  // decodeFailureCount: raw frames that failed FrameCodec::decodeFrame
  //   (bad CRC, wrong version, length mismatch, etc.) - i.e. frames
  //   that made it through ESP-NOW but arrived corrupted or truncated.
  // sequenceGapCount: kData frames whose header.sequence was not
  //   exactly one more than the previous kData frame's - each gap
  //   counts the number of frames apparently missing, so this is a
  //   count of *lost frames*, not lost gap events.
  // uartBytesWritten / uartWriteFailureCount: payload bytes actually
  //   handed to UartTransport::send(), and how many of those calls
  //   returned false (wrote fewer bytes than requested).
  uint32_t decodeFailureCount() const;
  uint32_t sequenceGapCount() const;
  uint32_t uartBytesWritten() const;
  uint32_t uartWriteFailureCount() const;

 private:
  EspNowDriver& espNow_;
  UartTransport& uart_;
  bool debugPrintHeartbeats_;
  bool debugPrintStats_;

  bool hasSequence_ = false;
  uint16_t expectedSequence_ = 0;

  uint32_t decodeFailures_ = 0;
  uint32_t sequenceGaps_ = 0;
  uint32_t uartBytesWritten_ = 0;
  uint32_t uartWriteFailures_ = 0;
};

}  // namespace transport
}  // namespace grut
