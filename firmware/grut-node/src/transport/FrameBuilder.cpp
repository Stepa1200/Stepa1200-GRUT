#include "transport/FrameBuilder.h"

#include <Arduino.h>

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
    lastByteMicros_ = micros();
    ++uartBytesRead_;

    if (chunkLength_ == kChunkPayloadBytes) {
      flush();
    }
  }

  // Flush only after a short idle gap, not unconditionally - see class
  // comment in FrameBuilder.h for why the unconditional version
  // fragmented every UART burst into near-single-byte GRUT frames.
  if (chunkLength_ > 0 &&
      (micros() - lastByteMicros_) >= kIdleFlushMicros) {
    flush();
  }
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
    ++framesSent_;
  }
  // If encoding somehow failed (shouldn't - chunkLength_ is always
  // <= kChunkPayloadBytes <= kMaxGrutPayloadBytes) or the send queue
  // was full, this chunk is silently dropped - consistent with ADR
  // 0005's "no lossless delivery guarantee" for v0.2.0. Either way,
  // reset the buffer so FrameBuilder never gets stuck.
  chunkLength_ = 0;
}

uint32_t FrameBuilder::uartBytesRead() const {
  return uartBytesRead_;
}

uint32_t FrameBuilder::framesSent() const {
  return framesSent_;
}

}  // namespace transport
}  // namespace grut
