#include "HelloCodec.h"

namespace grut {
namespace discovery {

size_t encodeHello(uint8_t* outBuffer, size_t outCapacity) {
  if (outBuffer == nullptr || outCapacity < kHelloPayloadSize) {
    return 0;
  }
  outBuffer[0] = kControlSubtypeHello;
  return kHelloPayloadSize;
}

bool decodeHello(const uint8_t* payload, size_t payloadLength) {
  if (payload == nullptr || payloadLength != kHelloPayloadSize) {
    return false;
  }
  return payload[0] == kControlSubtypeHello;
}

}  // namespace discovery
}  // namespace grut
