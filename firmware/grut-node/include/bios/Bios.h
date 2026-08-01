#pragma once

#include <cstddef>
#include <cstdint>

#include "RuntimeManager.h"
#include "bios/IConsole.h"
#include "grut/PhysicalUart.h"
#include "transport/ITransport.h"

namespace grut {
namespace bios {

constexpr const char* kBiosVersion = "0.1.0";

// GRUT BIOS.
//
// Owns boot, startup diagnostics, and command dispatch (help/status/
// reboot/transport). BIOS talks to the outside world only through
// IConsole (the interactive console) and ITransport (read-only status)
// - it never touches Serial directly. The "transport start"/"transport
// stop" console commands are the only place BIOS triggers a UART
// ownership change, and even then it only calls RuntimeManager, which
// alone performs the actual console.stop()/transport.start() (and
// reverse) sequence and rollback on failure (see lib/runtime_manager).
class Bios {
 public:
  Bios(IConsole& consoleRef, transport::ITransport& transportRef,
       RuntimeManager& runtimeManagerRef);

  // Call once from setup(). Starts the console (Transport stays stopped
  // until the "transport start" command or RuntimeManager::
  // enableTransport() is called) and runs startup diagnostics.
  void begin();

  // Call every loop() iteration.
  void loop();

 private:
  void handleConsoleLine(const char* line);
  void printHelp();
  void printStatus();
  void printTransportStatus();
  void handleTransportStart();
  void handleTransportStop();
  void reboot();

  IConsole& console_;
  transport::ITransport& transport_;
  RuntimeManager& runtimeManager_;
  char inputBuffer_[81] = {};
  size_t inputLength_ = 0;
  uint32_t bootMillis_ = 0;
};

}  // namespace bios
}  // namespace grut
