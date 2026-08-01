#include "bios/Diagnostics.h"

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace grut {
namespace bios {
namespace diagnostics {

namespace {

void writeCString(IConsole& console, const char* text) {
  console.write(reinterpret_cast<const uint8_t*>(text), strlen(text));
}

void writeLine(IConsole& console, const char* text) {
  writeCString(console, text);
  writeCString(console, "\r\n");
}

void writeLineFmt(IConsole& console, const char* fmt, ...) {
  char buf[96];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  writeLine(console, buf);
}

}  // namespace

void runStartupReport(IConsole& console) {
  writeLine(console, "");
  writeLine(console, "===================================");
  writeLine(console, "GRUT BIOS - startup diagnostics");
  writeLine(console, "===================================");

  writeLineFmt(console, "chip_id=0x%X", ESP.getChipId());
  writeLineFmt(console, "mac_address=%s", WiFi.macAddress().c_str());
  writeLineFmt(console, "flash_id=0x%X", ESP.getFlashChipId());
  writeLineFmt(console, "flash_real_size_kb=%u",
               static_cast<unsigned>(ESP.getFlashChipRealSize() / 1024));
  writeLineFmt(console, "flash_ide_size_kb=%u",
               static_cast<unsigned>(ESP.getFlashChipSize() / 1024));
  writeLineFmt(console, "flash_speed_mhz=%u",
               static_cast<unsigned>(ESP.getFlashChipSpeed() / 1000000));
  writeLineFmt(console, "cpu_freq_mhz=%u",
               static_cast<unsigned>(ESP.getCpuFreqMHz()));
  writeLineFmt(console, "free_heap_bytes=%u",
               static_cast<unsigned>(ESP.getFreeHeap()));
  writeLineFmt(console, "sketch_size_bytes=%u",
               static_cast<unsigned>(ESP.getSketchSize()));
  writeLineFmt(console, "free_sketch_space_bytes=%u",
               static_cast<unsigned>(ESP.getFreeSketchSpace()));
  writeLineFmt(console, "sdk_version=%s", ESP.getSdkVersion());
  writeLineFmt(console, "core_version=%s", ESP.getCoreVersion().c_str());
  writeLineFmt(console, "reset_reason=%s", ESP.getResetReason().c_str());
  writeLineFmt(console, "reset_info=%s", ESP.getResetInfo().c_str());

  const bool flashSizeMismatch =
      ESP.getFlashChipRealSize() != ESP.getFlashChipSize();

  writeLineFmt(console, "flash_size_ok=%s", flashSizeMismatch ? "no" : "yes");

  writeLine(console, "===================================");
  writeLine(console,
            flashSizeMismatch ? "diagnostics_warning" : "diagnostics_ok");
  writeLine(console, "===================================");
}

}  // namespace diagnostics
}  // namespace bios
}  // namespace grut
