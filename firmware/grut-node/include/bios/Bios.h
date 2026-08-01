#pragma once

#include <cstddef>
#include <cstdint>

#include "bios/IConsole.h"
#include "transport/ITransport.h"

namespace grut {
namespace bios {

constexpr const char* kBiosVersion = "0.1.0";
constexpr uint32_t kUartBaud = 57600;

// GRUT BIOS.
//
// Owns boot, startup diagnostics, and command dispatch (help/status/
// reboot). BIOS talks to the outside world only through IConsole (the
// interactive console) and ITransport (data) - it never touches Serial
// or any other physical peripheral directly. Per CLAUDE.md: UART belongs
// to Transport, and BIOS never writes to the physical UART while
// Transport is active. Swapping UartConsole for a TcpConsole later
// requires no change here.
class Bios {
 public:
  Bios(IConsole& consoleRef, transport::ITransport& transportRef);

  // Call once from setup(). Starts the console and transport, and runs
  // startup diagnostics.
  void begin();

  // Call every loop() iteration.
  void loop();

 private:
  void handleConsoleLine(const char* line);
  void printHelp();
  void printStatus();
  void reboot();

  IConsole& console_;
  transport::ITransport& transport_;
  char inputBuffer_[81] = {};
  size_t inputLength_ = 0;
  uint32_t bootMillis_ = 0;
};

}  // namespace bios
}  // namespace grut
