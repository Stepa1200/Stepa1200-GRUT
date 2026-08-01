#pragma once

#include <cstddef>
#include <cstdint>

namespace grut {
namespace protocol {

// CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no input/output
// reflection, no final XOR). Protects the entire GRUT frame (header +
// payload) with a 2-byte trailer - see docs/PROTOCOL.md.
//
// Known test vector: crc16CcittFalse("123456789", 9) == 0x29B1.
uint16_t crc16CcittFalse(const uint8_t* data, size_t length);

}  // namespace protocol
}  // namespace grut
