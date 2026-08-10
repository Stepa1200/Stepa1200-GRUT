#pragma once

#include <cstdint>

// Role (GRUT_BRIDGE_ROLE_AIR / GRUT_BRIDGE_ROLE_GROUND) and this
// node's GRUT protocol address (GRUT_NODE_ID) are supplied as
// PlatformIO build_flags by env:esp8285-air / env:esp8285-ground (see
// platformio.ini). Only bridge_main.cpp includes this header - the
// generic env:esp8285 (BIOS-only) build excludes bridge_main.cpp
// entirely and never reaches these checks.

#if defined(GRUT_BRIDGE_ROLE_AIR) && defined(GRUT_BRIDGE_ROLE_GROUND)
#error "Both GRUT_BRIDGE_ROLE_AIR and GRUT_BRIDGE_ROLE_GROUND are defined - pick one build environment"
#endif

#if !defined(GRUT_BRIDGE_ROLE_AIR) && !defined(GRUT_BRIDGE_ROLE_GROUND)
#error "No GRUT_BRIDGE_ROLE_* defined - build with env:esp8285-air or env:esp8285-ground"
#endif

#ifndef GRUT_NODE_ID
#error "GRUT_NODE_ID not defined - build with env:esp8285-air or env:esp8285-ground"
#endif

// Peer MAC addresses are kept out of version control - see
// config/local_bridge_config.h.example for the template. This file
// must exist locally (gitignored) before building env:esp8285-air or
// env:esp8285-ground.
#include "local_bridge_config.h"

namespace grut {

constexpr uint8_t kOwnAddr = GRUT_NODE_ID;

#if defined(GRUT_BRIDGE_ROLE_AIR)
constexpr uint8_t kPeerAddr = 2;  // GROUND
#else
constexpr uint8_t kPeerAddr = 1;  // AIR
#endif

}  // namespace grut
