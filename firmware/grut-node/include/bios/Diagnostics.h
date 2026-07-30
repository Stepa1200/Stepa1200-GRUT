#pragma once

namespace grut {
namespace bios {
namespace diagnostics {

// Prints a one-shot startup diagnostics report to Serial:
// chip id, flash size (real vs. configured), free heap, sketch size,
// SDK/core version, CPU frequency, and last reset reason.
//
// Serial must already be started (Serial.begin) before calling this.
void runStartupReport();

}  // namespace diagnostics
}  // namespace bios
}  // namespace grut
