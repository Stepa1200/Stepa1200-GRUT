#pragma once

#include <cstdint>

namespace grut {

// The single physical UART peripheral is shared, at different times, by
// bios::UartConsole and transport::UartTransport - never both at once
// (see lib/runtime_manager/RuntimeManager, CLAUDE.md: "UART belongs to
// Transport"). Both use the same baud rate since they take turns owning
// the exact same pins.
constexpr uint32_t kPhysicalUartBaud = 57600;

}  // namespace grut
