#include "transport/FrameBuilder.h"

#include "FrameCodec.h"
#include "GrutProtocol.h"

namespace grut {
namespace transport {

FrameBuilder::FrameBuilder(UartTransport& uart, EspNowDriver& espNow,
                            uint8_t srcAddr, uint8_t dstAddr)
    : uart_(uart), espNow_(espNow), srcAddr_(srcAddr), dstAddr_(dstAddr) {}

void FrameBuilder::poll() {
  while (uart_.available() > 0) {
    const int value = uart_.read();
    if (value < 0) {
      break;
    }

    chunkBuffer_[chunkLength_++] = static_cast<uint8_t>(value);

    if (chunkLength_ == kChunkPayloadBytes) {
      flush();
    }
  }

  // Nothing more available right now - send whatever's pending rather
  // than waiting for the chunk to fill, so small/infrequent messages
  // (e.g. MAVLink heartbeats) are not delayed.
  flush();
}

void FrameBuilder::flush() {
  if (chunkLength_ == 0) {
    return;
  }

  grut::protocol::GrutFrameHeader header;
  header.type = static_cast<uint8_t>(grut::protocol::PacketType::kData);
  header.srcAddr = srcAddr_;
  header.dstAddr = dstAddr_;
  header.sequence = nextSequence_++;

  uint8_t frameBuf[grut::protocol::kMaxFrameSizeBytes];
  const size_t frameLen =
      grut::protocol::encodeFrame(header, chunkBuffer_, chunkLength_,
                                   frameBuf, sizeof(frameBuf));

  if (frameLen > 0) {
    espNow_.send(frameBuf, frameLen);
  }
  // If encoding somehow failed (shouldn't - chunkLength_ is always
  // <= kChunkPayloadBytes <= kMaxGrutPayloadBytes) or the send queue
  // was full, this chunk is silently dropped - consistent with ADR
  // 0005's "no lossless delivery guarantee" for v0.2.0. Either way,
  // reset the buffer so FrameBuilder never gets stuck.
  chunkLength_ = 0;
}

}  // namespace transport
}  // namespace grut
