#pragma once

#include "bios/IConsole.h"
#include "transport/ITransport.h"

namespace grut {
namespace bios {

// Coordinates exclusive ownership of a shared physical link (UART today)
// between an IConsole and an ITransport. Never lets both run at once:
// enableTransport() always stops the console before starting Transport,
// and disableTransport() always stops Transport before restarting the
// console. If Transport fails to start, the console is restarted
// automatically (rollback), so BIOS is never left without any
// interface.
//
// See CLAUDE.md: "UART belongs to Transport; BIOS never writes to the
// physical UART while Transport is active."
//
// No Arduino dependency: this is pure coordination logic over the
// IConsole/ITransport interfaces, so it is host-testable
// (see test/test_runtime_manager).
class RuntimeManager {
 public:
  RuntimeManager(IConsole& console, transport::ITransport& transport);

  // Hands exclusive link ownership to Transport. Returns true if
  // Transport started successfully. On failure, restarts the console
  // and returns false - ownership stays with BIOS's console.
  bool enableTransport();

  // Returns link ownership to the console.
  void disableTransport();

  bool consoleRunning() const;
  bool transportRunning() const;

 private:
  IConsole& console_;
  transport::ITransport& transport_;
};

}  // namespace bios
}  // namespace grut
