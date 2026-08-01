#include <Arduino.h>

#include "RuntimeManager.h"
#include "bios/Bios.h"
#include "bios/UartConsole.h"
#include "transport/UartTransport.h"

namespace {

grut::bios::UartConsole gConsole;
grut::transport::UartTransport gTransport;
grut::bios::RuntimeManager gRuntimeManager(gConsole, gTransport);
grut::bios::Bios gBios(gConsole, gTransport, gRuntimeManager);

}  // namespace

void setup() {
  gBios.begin();
}

void loop() {
  gBios.loop();
}
