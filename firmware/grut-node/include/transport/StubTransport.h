#pragma once

#include "transport/ITransport.h"

namespace grut {
namespace transport {

// Disabled placeholder transport for GRUT BIOS v0.1.
//
// It exists only so BIOS can be built, flashed, and tested against a real
// ITransport implementation without committing to ESP-NOW, UDP, or mesh
// routing yet. Nothing here touches Wi-Fi, sockets, or radios:
// isEnabled() always reports false and send() always fails.
class StubTransport : public ITransport {
 public:
  void begin() override;
  void poll() override;
  bool isEnabled() const override;
  bool send(const uint8_t* data, size_t length) override;
  const char* name() const override;
};

}  // namespace transport
}  // namespace grut
