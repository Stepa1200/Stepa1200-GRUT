#pragma once

#include <cstdint>

// Role and peer MAC are supplied via PlatformIO per-node build_flags
// (see platformio.ini env:esp8285-air / env:esp8285-ground, ADR 0006).
// Only code that actually needs them includes this header - the
// generic env:esp8285 (BIOS testing) does not define these flags and
// must not include this header.

#define GRUT_ROLE_AIR 1
#define GRUT_ROLE_GROUND 2

#ifndef GRUT_NODE_ROLE
#error "GRUT_NODE_ROLE not defined - build with env:esp8285-air or env:esp8285-ground"
#endif

#ifndef GRUT_PEER_MAC
#error "GRUT_PEER_MAC not defined - build with env:esp8285-air or env:esp8285-ground"
#endif

namespace grut {

constexpr uint8_t kPeerMac[6] = GRUT_PEER_MAC;

// GRUT protocol-level addresses (1 byte, see docs/PROTOCOL.md) are
// simply the role numbers for this two-node bridge - no separate
// address config needed while there are only two possible values.
constexpr uint8_t kOwnAddr = GRUT_NODE_ROLE;
constexpr uint8_t kPeerAddr =
    (GRUT_NODE_ROLE == GRUT_ROLE_AIR) ? GRUT_ROLE_GROUND : GRUT_ROLE_AIR;

}  // namespace grut
