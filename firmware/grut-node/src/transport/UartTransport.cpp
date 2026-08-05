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
  // Nothing to do here anymore: byte-level draining is FrameBuilder's
  // job now (via available()/read()), not this driver's. Kept for
  // ITransport symmetry with EspNowDriver/IConsole, and reserved for
  // future housekeeping that isn't tied to a specific byte.
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

int UartTransport::available() {
  if (!running_) {
    return 0;
  }
  return Serial.available();
}

int UartTransport::read() {
  if (!running_) {
    return -1;
  }
  return Serial.read();
}

}  // namespace transport
}  // namespace grut
