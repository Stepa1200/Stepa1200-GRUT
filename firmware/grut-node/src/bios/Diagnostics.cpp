#include "bios/Diagnostics.h"

#include <Arduino.h>
#include <ESP8266WiFi.h>

namespace grut {
namespace bios {
namespace diagnostics {

void runStartupReport() {
  Serial.println();
  Serial.println(F("==================================="));
  Serial.println(F("GRUT BIOS - startup diagnostics"));
  Serial.println(F("==================================="));

  Serial.print(F("chip_id=0x"));
  Serial.println(ESP.getChipId(), HEX);

  Serial.print(F("mac_address="));
  Serial.println(WiFi.macAddress());

  Serial.print(F("flash_id=0x"));
  Serial.println(ESP.getFlashChipId(), HEX);

  Serial.print(F("flash_real_size_kb="));
  Serial.println(ESP.getFlashChipRealSize() / 1024);

  Serial.print(F("flash_ide_size_kb="));
  Serial.println(ESP.getFlashChipSize() / 1024);

  Serial.print(F("flash_speed_mhz="));
  Serial.println(ESP.getFlashChipSpeed() / 1000000);

  Serial.print(F("cpu_freq_mhz="));
  Serial.println(ESP.getCpuFreqMHz());

  Serial.print(F("free_heap_bytes="));
  Serial.println(ESP.getFreeHeap());

  Serial.print(F("sketch_size_bytes="));
  Serial.println(ESP.getSketchSize());

  Serial.print(F("free_sketch_space_bytes="));
  Serial.println(ESP.getFreeSketchSpace());

  Serial.print(F("sdk_version="));
  Serial.println(ESP.getSdkVersion());

  Serial.print(F("core_version="));
  Serial.println(ESP.getCoreVersion());

  Serial.print(F("reset_reason="));
  Serial.println(ESP.getResetReason());

  Serial.print(F("reset_info="));
  Serial.println(ESP.getResetInfo());

  bool flashSizeMismatch =
      ESP.getFlashChipRealSize() != ESP.getFlashChipSize();

  Serial.print(F("flash_size_ok="));
  Serial.println(flashSizeMismatch ? F("no") : F("yes"));

  Serial.println(F("==================================="));
  Serial.println(flashSizeMismatch ? F("diagnostics_warning")
                                    : F("diagnostics_ok"));
  Serial.println(F("==================================="));
}

}  // namespace diagnostics
}  // namespace bios
}  // namespace grut
