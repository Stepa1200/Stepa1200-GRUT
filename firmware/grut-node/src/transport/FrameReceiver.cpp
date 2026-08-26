#include "transport/FrameReceiver.h"

#include <cstdio>

#include "FrameCodec.h"
#include "GrutProtocol.h"

namespace grut {
namespace transport {

namespace {

uint32_t getU32LE(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

// Must match bridge_main.cpp's sendStats() field order exactly - this
// is ad hoc, TEMPORARY diagnostic wire format, not part of the GRUT
// protocol spec (docs/PROTOCOL.md). 12 uint32 fields, 48 bytes.
constexpr size_t kStatsFieldCount = 12;
constexpr size_t kStatsPayloadBytes = kStatsFieldCount * 4;

}  // namespace

FrameReceiver::FrameReceiver(EspNowDriver& espNow, UartTransport& uart,
                              bool debugPrintHeartbeats, bool debugPrintStats,
                              ValidFrameObserver validFrameObserver,
                              ControlFrameObserver controlFrameObserver,
                              DataSourceFilter dataSourceFilter)
    : espNow_(espNow),
      uart_(uart),
      debugPrintHeartbeats_(debugPrintHeartbeats),
      debugPrintStats_(debugPrintStats),
      validFrameObserver_(validFrameObserver),
      controlFrameObserver_(controlFrameObserver),
      dataSourceFilter_(dataSourceFilter) {}

void FrameReceiver::poll() {
  uint8_t rawFrame[grut::protocol::kMaxFrameSizeBytes];
  size_t rawLen = 0;
  uint8_t sourceMac[6];

  while (espNow_.receive(rawFrame, sizeof(rawFrame), &rawLen, sourceMac)) {
    grut::protocol::GrutFrameHeader header;
    uint8_t payload[grut::protocol::kMaxGrutPayloadBytes];
    size_t payloadLen = 0;

    const grut::protocol::DecodeResult result = grut::protocol::decodeFrame(
        rawFrame, rawLen, &header, payload, sizeof(payload), &payloadLen);

    if (result != grut::protocol::DecodeResult::kOk) {
      ++decodeFailures_;
      continue;  // corrupted/malformed - drop silently, no retransmission
    }

    // Stage 5.0: report (GRUT address, MAC) to Transport's endpoint
    // table for every successfully decoded frame, any type - this is
    // the only place both pieces are available together (EspNowDriver
    // knows the MAC but never decodes GRUT frames; this class decodes
    // but previously discarded the MAC). FrameReceiver does not store
    // or interpret the binding itself - see EspNowDriver::recordPeerBinding()
    // for the actual conflict policy.
    espNow_.recordPeerBinding(header.srcAddr, sourceMac);

    // sequence is node-wide and advances for every outbound GRUT frame,
    // regardless of packet type. Account for it before dispatching by type;
    // otherwise a HEARTBEAT/CONTROL frame between two DATA frames would look
    // like a false DATA loss on the next payload frame.
    if (hasSequence_) {
      const uint16_t expected = expectedSequence_;
      if (header.sequence != expected) {
        const uint16_t gap =
            static_cast<uint16_t>(header.sequence - expected);
        sequenceGaps_ += gap;
      }
    }
    expectedSequence_ = static_cast<uint16_t>(header.sequence + 1u);
    hasSequence_ = true;

    // Passive observation hook for LinkManager integration. This runs in the
    // normal main-loop context (never in the ESP-NOW callback), after a frame
    // has been validated but before packet-type dispatch. The observer cannot
    // alter payload delivery or acknowledge/drop the frame.
    if (validFrameObserver_ != nullptr) {
      validFrameObserver_(header.srcAddr, header.sequence, header.type);
    }

    if (header.type == static_cast<uint8_t>(grut::protocol::PacketType::kHeartbeat)) {
      if (debugPrintHeartbeats_) {
        const uint8_t marker[] = {'H', 'B', '\r', '\n'};
        uart_.send(marker, sizeof(marker));
      }
      continue;  // heartbeat carries no UART payload either way
    }

    if (header.type == static_cast<uint8_t>(grut::protocol::PacketType::kControl)) {
      if (controlFrameObserver_ != nullptr) {
        controlFrameObserver_(header.srcAddr, payload, payloadLen);
      }
      if (debugPrintStats_ && payloadLen == kStatsPayloadBytes) {
        uint32_t f[kStatsFieldCount];
        for (size_t i = 0; i < kStatsFieldCount; ++i) {
          f[i] = getU32LE(payload + i * 4);
        }
        char line[192];
        const int len = snprintf(
            line, sizeof(line),
            "STATS src=%u rd=%lu tx=%lu txAttempt=%lu txErr=%lu txOk=%lu "
            "txFail=%lu txDrop=%lu rxDrop=%lu dec=%lu gap=%lu wr=%lu "
            "wrFail=%lu\r\n",
            static_cast<unsigned>(header.srcAddr),
            static_cast<unsigned long>(f[0]), static_cast<unsigned long>(f[1]),
            static_cast<unsigned long>(f[2]), static_cast<unsigned long>(f[3]),
            static_cast<unsigned long>(f[4]), static_cast<unsigned long>(f[5]),
            static_cast<unsigned long>(f[6]), static_cast<unsigned long>(f[7]),
            static_cast<unsigned long>(f[8]), static_cast<unsigned long>(f[9]),
            static_cast<unsigned long>(f[10]), static_cast<unsigned long>(f[11]));
        if (len > 0) {
          uart_.send(reinterpret_cast<const uint8_t*>(line),
                     static_cast<size_t>(len));
        }
      }
      continue;  // control frames never carry UART payload
    }

    if (header.type != static_cast<uint8_t>(grut::protocol::PacketType::kData)) {
      continue;  // unknown type - reserved, not handled yet
    }

    // Stage 4.2 safety gate: discovery (NeighborTable) is separate from
    // DATA authorization. A frame can be well-formed, in-sequence, and
    // from a known/discovered neighbor, and still never reach UART if
    // it isn't from the configured active DATA peer.
    if (dataSourceFilter_ != nullptr && !dataSourceFilter_(header.srcAddr)) {
      ++dataWrongSourceDrops_;
      continue;
    }

    if (payloadLen > 0) {
      const size_t written = uart_.send(payload, payloadLen) ? payloadLen : 0;
      uartBytesWritten_ += written;
      if (written != payloadLen) {
        ++uartWriteFailures_;
      }
    }
  }
}

uint32_t FrameReceiver::decodeFailureCount() const {
  return decodeFailures_;
}

uint32_t FrameReceiver::sequenceGapCount() const {
  return sequenceGaps_;
}

uint32_t FrameReceiver::uartBytesWritten() const {
  return uartBytesWritten_;
}

uint32_t FrameReceiver::uartWriteFailureCount() const {
  return uartWriteFailures_;
}

uint32_t FrameReceiver::dataWrongSourceDropCount() const {
  return dataWrongSourceDrops_;
}

}  // namespace transport
}  // namespace grut
