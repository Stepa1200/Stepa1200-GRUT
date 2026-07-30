#pragma once

#include <Arduino.h>
#include <cstdint>

#include "transport/ITransport.h"

namespace grut {
namespace bios {

constexpr const char* kBiosVersion = "0.1.0";
constexpr uint32_t kUartBaud = 57600;

// GRUT BIOS v0.1.
//
// Owns boot, startup diagnostics, and the UART console. BIOS holds only a
// reference to transport::ITransport - never a concrete transport type -
// so it can run, build, and be tested with no real transport wired in
// (see transport::StubTransport). Role management, mesh, and Wi-Fi are
// intentionally out of scope for v0.1.
class Bios {
 public:
  explicit Bios(transport::ITransport& transportRef);

  // Call once from setup(). Starts Serial at kUartBaud and runs
  // diagnostics + transport::begin().
  void begin();

  // Call every loop() iteration.
  void loop();

 private:
  void handleConsoleLine(const String& line);
  void printHelp();
  void printStatus();
  void reboot();

  transport::ITransport& transport_;
  String inputBuffer_;
  uint32_t bootMillis_ = 0;
};

}  // namespace bios
}  // namespace grut
