#pragma once

#include "transport/ITransport.h"

namespace grut {
namespace transport {

// Real UART transport: claims the physical UART peripheral exclusively
// while running.
//
// This milestone only proves exclusive ownership of the physical link -
// it does not yet bridge or parse any protocol (MAVLink framing,
// routing, ESP-NOW, etc. are separate future milestones). While
// running, poll() drains any incoming bytes without interpreting them,
// and send() writes the caller's raw bytes out; nothing here decides
// *what* to send yet.
//
// Per CLAUDE.md: "Transport must not print uncontrolled text into
// MAVLink UART" - poll()/send() never write diagnostic text, only the
// caller-provided payload bytes.
class UartTransport : public ITransport {
 public:
  bool start() override;
  void stop() override;
  bool isRunning() const override;
  void poll() override;
  bool send(const uint8_t* data, size_t length) override;
  const char* name() const override;

 private:
  bool running_ = false;
};

}  // namespace transport
}  // namespace grut
