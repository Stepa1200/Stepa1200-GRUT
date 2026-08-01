#pragma once

#include "transport/ITransport.h"

namespace grut {
namespace transport {

// Disabled placeholder transport.
//
// Not wired into main.cpp for the physical UART anymore - that link now
// has a real implementation (see UartTransport). Kept as the reference
// "always disabled" ITransport for future links (ESP-NOW, UDP, ...)
// that don't have a real transport yet.
class StubTransport : public ITransport {
 public:
  bool start() override;
  void stop() override;
  bool isRunning() const override;
  void poll() override;
  bool send(const uint8_t* data, size_t length) override;
  const char* name() const override;
};

}  // namespace transport
}  // namespace grut
