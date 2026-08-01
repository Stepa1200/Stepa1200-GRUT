#include "bios/Bios.h"

#include <Arduino.h>  // millis(), delay(), ESP.* - not Serial
#include <cctype>
#include <cstdio>
#include <cstring>

#include "CommandParser.h"
#include "bios/Diagnostics.h"

namespace grut {
namespace bios {

namespace {

void writeCString(IConsole& console, const char* text) {
  console.write(reinterpret_cast<const uint8_t*>(text), strlen(text));
}

void writeLine(IConsole& console, const char* text) {
  writeCString(console, text);
  writeCString(console, "\r\n");
}

}  // namespace

Bios::Bios(IConsole& consoleRef, transport::ITransport& transportRef)
    : console_(consoleRef), transport_(transportRef) {}

void Bios::begin() {
  console_.begin();

  bootMillis_ = millis();

  diagnostics::runStartupReport(console_);

  transport_.begin();

  char lineBuf[64];
  snprintf(lineBuf, sizeof(lineBuf), "transport=%s", transport_.name());
  writeLine(console_, lineBuf);
  writeLine(console_, transport_.isEnabled() ? "transport_enabled=yes"
                                              : "transport_enabled=no");

  writeLine(console_, "");
  snprintf(lineBuf, sizeof(lineBuf), "GRUT BIOS v%s", kBiosVersion);
  writeLine(console_, lineBuf);
  writeLine(console_, "Type 'help' for a list of commands.");
  writeCString(console_, "> ");
}

void Bios::loop() {
  transport_.poll();

  while (console_.available() > 0) {
    const int value = console_.read();
    if (value < 0) {
      break;
    }

    const char ch = static_cast<char>(value);

    if (ch == '\r') {
      continue;
    }

    if (ch == '\n') {
      writeLine(console_, "");
      inputBuffer_[inputLength_] = '\0';
      handleConsoleLine(inputBuffer_);
      inputLength_ = 0;
      writeCString(console_, "> ");
    } else if (ch == 8 || ch == 127) {
      // Backspace/DEL.
      if (inputLength_ > 0) {
        --inputLength_;
        writeCString(console_, "\b \b");
      }
    } else if (inputLength_ < sizeof(inputBuffer_) - 1) {
      inputBuffer_[inputLength_++] = ch;
      console_.write(reinterpret_cast<const uint8_t*>(&ch), 1);
    }
  }
}

void Bios::handleConsoleLine(const char* rawLine) {
  // Trim + lowercase into a local buffer before dispatch - same behavior
  // BIOS used to get from Arduino String::trim()/toLowerCase(), without
  // depending on String here.
  const size_t rawLen = strlen(rawLine);

  size_t start = 0;
  while (start < rawLen &&
         isspace(static_cast<unsigned char>(rawLine[start]))) {
    ++start;
  }

  size_t end = rawLen;
  while (end > start &&
         isspace(static_cast<unsigned char>(rawLine[end - 1]))) {
    --end;
  }

  char line[82];
  size_t outLen = end - start;
  if (outLen >= sizeof(line)) {
    outLen = sizeof(line) - 1;
  }
  for (size_t i = 0; i < outLen; ++i) {
    line[i] =
        static_cast<char>(tolower(static_cast<unsigned char>(rawLine[start + i])));
  }
  line[outLen] = '\0';

  switch (parseCommandLine(std::string(line))) {
    case Command::kEmpty:
      return;
    case Command::kHelp:
      printHelp();
      return;
    case Command::kStatus:
      printStatus();
      return;
    case Command::kReboot:
      reboot();
      return;
    case Command::kUnknown: {
      char buf[128];
      snprintf(buf, sizeof(buf), "unknown command: %s", line);
      writeLine(console_, buf);
      writeLine(console_, "type 'help' for a list of commands");
      return;
    }
  }
}

void Bios::printHelp() {
  writeLine(console_, "Available commands:");
  writeLine(console_, "  help    - show this message");
  writeLine(console_, "  status  - show BIOS/transport status");
  writeLine(console_, "  reboot  - restart the device");
}

void Bios::printStatus() {
  writeLine(console_, "--- GRUT BIOS status ---");

  char buf[64];

  snprintf(buf, sizeof(buf), "bios_version=%s", kBiosVersion);
  writeLine(console_, buf);

  snprintf(buf, sizeof(buf), "uptime_ms=%lu",
            static_cast<unsigned long>(millis() - bootMillis_));
  writeLine(console_, buf);

  snprintf(buf, sizeof(buf), "uart_baud=%lu",
            static_cast<unsigned long>(kUartBaud));
  writeLine(console_, buf);

  snprintf(buf, sizeof(buf), "free_heap_bytes=%lu",
            static_cast<unsigned long>(ESP.getFreeHeap()));
  writeLine(console_, buf);

  snprintf(buf, sizeof(buf), "transport=%s", transport_.name());
  writeLine(console_, buf);

  writeLine(console_, transport_.isEnabled() ? "transport_enabled=yes"
                                              : "transport_enabled=no");
}

void Bios::reboot() {
  writeLine(console_, "rebooting...");
  console_.flush();
  delay(100);
  ESP.restart();
}

}  // namespace bios
}  // namespace grut
