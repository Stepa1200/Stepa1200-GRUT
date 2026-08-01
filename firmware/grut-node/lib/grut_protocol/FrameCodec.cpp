#include "FrameCodec.h"

#include <cstring>

#include "Crc16.h"

namespace grut {
namespace protocol {

namespace {

void putU16LE(uint8_t* dst, uint16_t value) {
  dst[0] = static_cast<uint8_t>(value & 0xFF);
  dst[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

uint16_t getU16LE(const uint8_t* src) {
  return static_cast<uint16_t>(src[0]) |
         static_cast<uint16_t>(static_cast<uint16_t>(src[1]) << 8);
}

}  // namespace

size_t encodeFrame(GrutFrameHeader header, const uint8_t* payload,
                    size_t payloadLength, uint8_t* out, size_t outCapacity) {
  if (payloadLength > kMaxGrutPayloadBytes) {
    return 0;
  }

  const size_t frameLength = kHeaderWireSize + payloadLength + kCrcWireSize;
  if (frameLength > outCapacity || frameLength > kMaxFrameSizeBytes) {
    return 0;
  }

  header.payloadLength = static_cast<uint8_t>(payloadLength);

  size_t offset = 0;
  out[offset++] = header.version;
  out[offset++] = header.type;
  out[offset++] = header.flags;
  out[offset++] = header.srcAddr;
  out[offset++] = header.dstAddr;
  out[offset++] = header.ttl;
  putU16LE(out + offset, header.sequence);
  offset += 2;
  out[offset++] = header.payloadLength;

  if (payloadLength > 0) {
    std::memcpy(out + offset, payload, payloadLength);
  }
  offset += payloadLength;

  const uint16_t crc = crc16CcittFalse(out, offset);
  putU16LE(out + offset, crc);
  offset += kCrcWireSize;

  return offset;
}

DecodeResult decodeFrame(const uint8_t* data, size_t dataLength,
                          GrutFrameHeader* outHeader, uint8_t* outPayload,
                          size_t outPayloadCapacity,
                          size_t* outPayloadLength) {
  if (dataLength < kHeaderWireSize + kCrcWireSize) {
    return DecodeResult::kTooShort;
  }
  if (dataLength > kMaxFrameSizeBytes) {
    return DecodeResult::kTooLong;
  }

  size_t offset = 0;
  GrutFrameHeader header;
  header.version = data[offset++];
  header.type = data[offset++];
  header.flags = data[offset++];
  header.srcAddr = data[offset++];
  header.dstAddr = data[offset++];
  header.ttl = data[offset++];
  header.sequence = getU16LE(data + offset);
  offset += 2;
  header.payloadLength = data[offset++];

  if (header.version != kProtocolVersion) {
    return DecodeResult::kUnsupportedVersion;
  }

  const size_t expectedLength =
      kHeaderWireSize + header.payloadLength + kCrcWireSize;
  if (expectedLength != dataLength) {
    return DecodeResult::kLengthMismatch;
  }

  if (header.payloadLength > outPayloadCapacity) {
    return DecodeResult::kPayloadBufferTooSmall;
  }

  const uint16_t receivedCrc =
      getU16LE(data + kHeaderWireSize + header.payloadLength);
  const uint16_t computedCrc =
      crc16CcittFalse(data, kHeaderWireSize + header.payloadLength);
  if (receivedCrc != computedCrc) {
    return DecodeResult::kCrcMismatch;
  }

  if (header.payloadLength > 0) {
    std::memcpy(outPayload, data + kHeaderWireSize, header.payloadLength);
  }

  *outHeader = header;
  *outPayloadLength = header.payloadLength;
  return DecodeResult::kOk;
}

}  // namespace protocol
}  // namespace grut
