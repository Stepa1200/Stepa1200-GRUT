#pragma once

#include <cstddef>
#include <cstdint>

#include "GrutProtocol.h"

namespace grut {
namespace protocol {

enum class DecodeResult : uint8_t {
  kOk = 0,
  kTooShort,               // buffer smaller than the minimum possible frame
  kTooLong,                // buffer larger than kMaxFrameSizeBytes
  kUnsupportedVersion,
  kLengthMismatch,         // header.payloadLength doesn't match dataLength
  kPayloadBufferTooSmall,  // caller-provided output buffer too small
  kCrcMismatch,
};

// Serializes header+payload into `out` (capacity `outCapacity`).
// header.payloadLength is set internally from `payloadLength` - any
// value the caller put there is overwritten.
//
// Returns the total encoded frame length (header + payload + CRC
// trailer) on success, or 0 if payloadLength exceeds
// kMaxGrutPayloadBytes, or the resulting frame would not fit in
// outCapacity.
size_t encodeFrame(GrutFrameHeader header, const uint8_t* payload,
                    size_t payloadLength, uint8_t* out, size_t outCapacity);

// Parses a wire-format frame from `data` (length `dataLength`).
// On kOk, *outHeader is filled and up to outPayloadCapacity bytes of
// payload are copied into outPayload, with *outPayloadLength set to the
// actual payload size. On any other result, outHeader/outPayload
// contents are unspecified.
DecodeResult decodeFrame(const uint8_t* data, size_t dataLength,
                          GrutFrameHeader* outHeader, uint8_t* outPayload,
                          size_t outPayloadCapacity,
                          size_t* outPayloadLength);

}  // namespace protocol
}  // namespace grut
