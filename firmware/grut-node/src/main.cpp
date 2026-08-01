#include <Arduino.h>

#include "bios/Bios.h"
#include "bios/UartConsole.h"
#include "transport/StubTransport.h"

namespace {

grut::bios::UartConsole gConsole;
grut::transport::StubTransport gTransport;
grut::bios::Bios gBios(gConsole, gTransport);

}  // namespace

void setup() {
  gBios.begin();
}

void loop() {
  gBios.loop();
}
