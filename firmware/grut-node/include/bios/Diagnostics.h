#pragma once

#include "bios/IConsole.h"

namespace grut {
namespace bios {
namespace diagnostics {

// Prints a one-shot startup diagnostics report through the given console:
// chip id, MAC address, flash size (real vs. configured), free heap,
// sketch size, SDK/core version, CPU frequency, and last reset reason.
//
// console must already be started (console.begin()) before calling this.
void runStartupReport(IConsole& console);

}  // namespace diagnostics
}  // namespace bios
}  // namespace grut
