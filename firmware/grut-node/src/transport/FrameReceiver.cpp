#include "transport/FrameReceiver.h"

#include "FrameCodec.h"
#include "GrutProtocol.h"

namespace grut {
namespace transport {

FrameReceiver::FrameReceiver(EspNowDriver& espNow, UartTransport& uart)
    : espNow_(espNow), uart_(uart) {}

void FrameReceiver::poll() {
  uint8_t rawFrame[grut::protocol::kMaxFrameSizeBytes];
  size_t rawLen = 0;

  while (espNow_.receive(rawFrame, sizeof(rawFrame), &rawLen)) {
    grut::protocol::GrutFrameHeader header;
    uint8_t payload[grut::protocol::kMaxGrutPayloadBytes];
    size_t payloadLen = 0;

    const grut::protocol::DecodeResult result = grut::protocol::decodeFrame(
        rawFrame, rawLen, &header, payload, sizeof(payload), &payloadLen);

    if (result != grut::protocol::DecodeResult::kOk) {
      continue;  // corrupted/malformed - drop silently, no retransmission
    }

    if (header.type != static_cast<uint8_t>(grut::protocol::PacketType::kData)) {
      continue;  // heartbeat/control - reserved, not handled yet
    }

    if (payloadLen > 0) {
      uart_.send(payload, payloadLen);
    }
  }
}

}  // namespace transport
}  // namespace grut
