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
// today via UartConsole, TCP later via TcpConsole). See CLAUDE.md:
// "UART belongs to Transport, BIOS never writes to the physical UART
// while Transport is active."
class IConsole {
 public:
  virtual ~IConsole() = default;

  // Called once from Bios::begin(). Must not block.
  virtual void begin() = 0;

  // Number of bytes currently available to read.
  virtual int available() = 0;

  // Read one byte, or -1 if none is available.
  virtual int read() = 0;

  // Write raw bytes. Returns the number of bytes actually written.
  virtual size_t write(const uint8_t* data, size_t length) = 0;

  // Block until all previously written bytes have been physically
  // transmitted. Needed before Bios::reboot() so the "rebooting..."
  // message is not cut off by ESP.restart().
  virtual void flush() = 0;
};

}  // namespace bios
}  // namespace grut
