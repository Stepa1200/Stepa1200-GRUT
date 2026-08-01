#pragma once

#include <cstddef>
#include <cstdint>

namespace grut {
namespace transport {

// GRUT Transport interface.
//
// BIOS never talks to a radio, socket, or UART directly - it only ever
// holds a reference to an ITransport (for read-only status) while
// RuntimeManager is the only thing that calls start()/stop() on it.
// Concrete transports (UartTransport, ESP-NOW, UDP, mesh routing...)
// implement this and are injected. StubTransport is the disabled
// placeholder used for links that don't have a real transport yet.
//
// Lifecycle: start()/stop()/isRunning() mirror bios::IConsole so
// RuntimeManager can treat both as symmetric, exclusively-owned
// resources over the same physical link.
class ITransport {
 public:
  virtual ~ITransport() = default;

  // Claims the underlying link (e.g. Serial.begin()) and starts
  // carrying data. Returns false if the link could not be claimed -
  // RuntimeManager rolls back to the console in that case.
  virtual bool start() = 0;

  // Releases the underlying link (e.g. Serial.end()). Idempotent.
  virtual void stop() = 0;

  virtual bool isRunning() const = 0;

  // Called every loop() iteration while running. Must not block, and
  // must never write uncontrolled/diagnostic text into the link (see
  // CLAUDE.md: "Transport must not print uncontrolled text into MAVLink
  // UART").
  virtual void poll() = 0;

  // Send a raw buffer through the transport. Returns false if not
  // running or the send failed.
  virtual bool send(const uint8_t* data, size_t length) = 0;

  // Short, human-readable identifier for diagnostics ("none", "uart",
  // "espnow", ...).
  virtual const char* name() const = 0;
};

}  // namespace transport
}  // namespace grut
