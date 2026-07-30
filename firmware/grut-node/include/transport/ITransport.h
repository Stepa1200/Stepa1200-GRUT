#pragma once

#include <cstddef>
#include <cstdint>

namespace grut {
namespace transport {

// GRUT Transport interface.
//
// GRUT BIOS never talks to a radio, socket, or mesh directly - it only
// ever holds a reference to an ITransport. Concrete transports (ESP-NOW,
// UDP, TCP console bridge, mesh routing...) are implemented separately and
// injected into Bios. This is what "BIOS and Transport separated by
// interfaces" means in practice: BIOS can be built, flashed, and tested
// with zero real transports wired in, using transport::StubTransport.
class ITransport {
 public:
  virtual ~ITransport() = default;

  // Called once from setup(). Must not block.
  virtual void begin() = 0;

  // Called every loop() iteration. Must not block.
  virtual void poll() = 0;

  // True once the transport is initialized and able to carry data.
  // The stub implementation always returns false.
  virtual bool isEnabled() const = 0;

  // Send a raw buffer through the transport.
  // Returns false if the transport is disabled or the send failed.
  virtual bool send(const uint8_t* data, size_t length) = 0;

  // Short, human-readable identifier used in diagnostics and the
  // "status" console command (e.g. "none", "espnow", "udp").
  virtual const char* name() const = 0;
};

}  // namespace transport
}  // namespace grut
