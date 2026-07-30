#include <Arduino.h>

#include "bios/Bios.h"
#include "transport/StubTransport.h"

namespace {

grut::transport::StubTransport gTransport;
grut::bios::Bios gBios(gTransport);

}  // namespace

void setup() {
  gBios.begin();
}

void loop() {
  gBios.loop();
}
