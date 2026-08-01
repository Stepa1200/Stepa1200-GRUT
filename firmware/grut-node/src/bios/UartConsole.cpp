#include "bios/UartConsole.h"

#include <Arduino.h>

#include "bios/Bios.h"  // kUartBaud

namespace grut {
namespace bios {

void UartConsole::begin() {
  Serial.begin(kUartBaud);
  delay(100);
}

int UartConsole::available() {
  return Serial.available();
}

int UartConsole::read() {
  return Serial.read();
}

size_t UartConsole::write(const uint8_t* data, size_t length) {
  return Serial.write(data, length);
}

void UartConsole::flush() {
  Serial.flush();
}

}  // namespace bios
}  // namespace grut
