#include "bios/Bios.h"

#include "bios/Diagnostics.h"

namespace grut {
namespace bios {

Bios::Bios(transport::ITransport& transportRef) : transport_(transportRef) {}

void Bios::begin() {
  Serial.begin(kUartBaud);
  delay(100);

  bootMillis_ = millis();

  diagnostics::runStartupReport();

  transport_.begin();

  Serial.print(F("transport="));
  Serial.println(transport_.name());
  Serial.print(F("transport_enabled="));
  Serial.println(transport_.isEnabled() ? F("yes") : F("no"));

  Serial.println();
  Serial.print(F("GRUT BIOS v"));
  Serial.println(kBiosVersion);
  Serial.println(F("Type 'help' for a list of commands."));
  Serial.print(F("> "));
}

void Bios::loop() {
  transport_.poll();

  while (Serial.available() > 0) {
    const int value = Serial.read();
    if (value < 0) {
      break;
    }

    const char ch = static_cast<char>(value);

    if (ch == '\r') {
      continue;
    }

    if (ch == '\n') {
      Serial.println();
      handleConsoleLine(inputBuffer_);
      inputBuffer_ = "";
      Serial.print(F("> "));
    } else if (ch == 8 || ch == 127) {
      // Backspace/DEL.
      if (inputBuffer_.length() > 0) {
        inputBuffer_.remove(inputBuffer_.length() - 1);
        Serial.print(F("\b \b"));
      }
    } else {
      inputBuffer_ += ch;
      Serial.print(ch);
    }
  }
}

void Bios::handleConsoleLine(const String& rawLine) {
  String line = rawLine;
  line.trim();
  line.toLowerCase();

  if (line.length() == 0) {
    return;
  }

  if (line == "help" || line == "?") {
    printHelp();
  } else if (line == "status") {
    printStatus();
  } else if (line == "reboot") {
    reboot();
  } else {
    Serial.print(F("unknown command: "));
    Serial.println(line);
    Serial.println(F("type 'help' for a list of commands"));
  }
}

void Bios::printHelp() {
  Serial.println(F("Available commands:"));
  Serial.println(F("  help    - show this message"));
  Serial.println(F("  status  - show BIOS/transport status"));
  Serial.println(F("  reboot  - restart the device"));
}

void Bios::printStatus() {
  Serial.println(F("--- GRUT BIOS status ---"));

  Serial.print(F("bios_version="));
  Serial.println(kBiosVersion);

  Serial.print(F("uptime_ms="));
  Serial.println(millis() - bootMillis_);

  Serial.print(F("uart_baud="));
  Serial.println(kUartBaud);

  Serial.print(F("free_heap_bytes="));
  Serial.println(ESP.getFreeHeap());

  Serial.print(F("transport="));
  Serial.println(transport_.name());

  Serial.print(F("transport_enabled="));
  Serial.println(transport_.isEnabled() ? F("yes") : F("no"));
}

void Bios::reboot() {
  Serial.println(F("rebooting..."));
  delay(100);
  Serial.flush();
  ESP.restart();
}

}  // namespace bios
}  // namespace grut
