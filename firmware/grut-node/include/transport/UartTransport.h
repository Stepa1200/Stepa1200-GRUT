#pragma once

#include "transport/ITransport.h"

namespace grut {
namespace transport {

// Real UART transport: claims the physical UART peripheral exclusively
// while running.
//
// send()/available()/read() move raw bytes; this class does not parse
// or interpret them (MAVLink framing, GRUT frame encoding, etc. are
// FrameBuilder/FrameReceiver's job, not this driver's - ADR 0001).
//
// available()/read() are deliberately NOT part of ITransport: that
// interface is meant to stay transport-agnostic (ESP-NOW, UDP, LoRa...
// per ADR 0005), and "raw byte stream in" is a UART-specific concept
// that only FrameBuilder (which is inherently UART-specific already)
// needs. Callers that only need generic lifecycle/send use ITransport;
// callers that need to read incoming UART bytes hold a UartTransport&
// directly.
//
// Per CLAUDE.md: "Transport must not print uncontrolled text into
// MAVLink UART" - nothing here writes diagnostic text, only the
// caller-provided payload bytes (send()) or physical UART bytes
// (available()/read()).
class UartTransport : public ITransport {
 public:
  bool start() override;
  void stop() override;
  bool isRunning() const override;
  void poll() override;
  bool send(const uint8_t* data, size_t length) override;
  const char* name() const override;

  // Number of bytes currently available to read. Returns 0 while
  // stopped.
  int available();

  // Read one byte, or -1 if none is available or the transport is
  // stopped.
  int read();

 private:
  bool running_ = false;
};

}  // namespace transport
}  // namespace grut
