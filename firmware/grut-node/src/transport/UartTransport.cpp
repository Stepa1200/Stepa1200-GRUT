#include "transport/UartTransport.h"

#include <Arduino.h>

#include "grut/PhysicalUart.h"

namespace grut {
namespace transport {

bool UartTransport::start() {
  if (running_) {
    return true;
  }
  Serial.begin(grut::kPhysicalUartBaud);
  running_ = true;
  return true;
}

void UartTransport::stop() {
  if (!running_) {
    return;
  }
  Serial.end();
  running_ = false;
}

bool UartTransport::isRunning() const {
  return running_;
}

void UartTransport::poll() {
  if (!running_) {
    return;
  }
  // Drain incoming bytes silently. No protocol/bridging logic yet - see
  // class comment. This only proves UartTransport can exclusively own
  // the physical UART while active.
  while (Serial.available() > 0) {
    Serial.read();
  }
}

bool UartTransport::send(const uint8_t* data, size_t length) {
  if (!running_) {
    return false;
  }
  return Serial.write(data, length) == length;
}

const char* UartTransport::name() const {
  return "uart";
}

}  // namespace transport
}  // namespace grut
