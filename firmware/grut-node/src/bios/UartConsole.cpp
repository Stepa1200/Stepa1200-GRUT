#include "bios/UartConsole.h"

#include <Arduino.h>

#include "grut/PhysicalUart.h"

namespace grut {
namespace bios {

bool UartConsole::start() {
  if (running_) {
    return true;
  }
  Serial.begin(grut::kPhysicalUartBaud);
  delay(100);
  running_ = true;
  return true;
}

void UartConsole::stop() {
  if (!running_) {
    return;
  }
  Serial.end();
  running_ = false;
}

bool UartConsole::isRunning() const {
  return running_;
}

void UartConsole::poll() {
  // Nothing to do: byte-level I/O is serviced directly by Bios::loop()
  // via available()/read()/write(). Reserved for future console types
  // (e.g. TcpConsole accepting connections) that need periodic servicing.
}

int UartConsole::available() {
  if (!running_) {
    return 0;
  }
  return Serial.available();
}

int UartConsole::read() {
  if (!running_) {
    return -1;
  }
  return Serial.read();
}

size_t UartConsole::write(const uint8_t* data, size_t length) {
  if (!running_) {
    return 0;
  }
  return Serial.write(data, length);
}

void UartConsole::flush() {
  if (!running_) {
    return;
  }
  Serial.flush();
}

}  // namespace bios
}  // namespace grut
