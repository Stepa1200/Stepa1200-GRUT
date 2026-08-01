#pragma once

#include <cstddef>
#include <cstdint>

namespace grut {
namespace bios {

// Abstract console channel used by BIOS for interactive help/status/
// reboot commands.
//
// Deliberately low-level: raw bytes only, no String, no println. Line
// assembly, trimming, and command dispatch stay in Bios/CommandParser -
// this interface has zero knowledge of how those bytes travel (UART
// today via UartConsole, TCP later via TcpConsole).
//
// Lifecycle: start()/stop()/isRunning() let RuntimeManager hand
// exclusive ownership of the underlying physical link (UART today) to
// Transport and back. See CLAUDE.md: "UART belongs to Transport, BIOS
// never writes to the physical UART while Transport is active."
class IConsole {
 public:
  virtual ~IConsole() = default;

  // Claims the underlying link and makes the console active. Idempotent:
  // calling start() while already running must be a safe no-op.
  // Returns false if the link could not be claimed.
  virtual bool start() = 0;

  // Releases the underlying link. Idempotent: calling stop() while
  // already stopped must be a safe no-op.
  virtual void stop() = 0;

  virtual bool isRunning() const = 0;

  // Services housekeeping not tied to a specific byte (e.g. accepting
  // new connections for a future TcpConsole). No-op for UartConsole.
  // Must not block.
  virtual void poll() = 0;

  // Number of bytes currently available to read. Must return 0 while
  // stopped.
  virtual int available() = 0;

  // Read one byte, or -1 if none is available or the console is
  // stopped.
  virtual int read() = 0;

  // Write raw bytes. Returns the number of bytes actually written, 0 if
  // the console is stopped.
  virtual size_t write(const uint8_t* data, size_t length) = 0;

  // Block until all previously written bytes have been physically
  // transmitted. No-op if stopped. Needed before Bios::reboot() so the
  // "rebooting..." message is not cut off by ESP.restart().
  virtual void flush() = 0;
};

}  // namespace bios
}  // namespace grut
