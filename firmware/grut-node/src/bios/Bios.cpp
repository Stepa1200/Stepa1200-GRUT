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

Bios::Bios(IConsole& consoleRef, transport::ITransport& transportRef,
           RuntimeManager& runtimeManagerRef)
    : console_(consoleRef),
      transport_(transportRef),
      runtimeManager_(runtimeManagerRef) {}

void Bios::begin() {
  console_.start();

  bootMillis_ = millis();

  diagnostics::runStartupReport(console_);

  // Transport is intentionally NOT started here. It stays stopped until
  // RuntimeManager::enableTransport() is called - starting it
  // unconditionally at boot would race the console for the same
  // physical UART.
  char lineBuf[64];
  snprintf(lineBuf, sizeof(lineBuf), "transport_name=%s", transport_.name());
  writeLine(console_, lineBuf);
  writeLine(console_,
            transport_.isRunning() ? "transport=running" : "transport=stopped");

  writeLine(console_, "");
  snprintf(lineBuf, sizeof(lineBuf), "GRUT BIOS v%s", kBiosVersion);
  writeLine(console_, lineBuf);
  writeLine(console_, "Type 'help' for a list of commands.");
  writeCString(console_, "> ");
}

void Bios::loop() {
  transport_.poll();

  // While the console is stopped (Transport active), available() always
  // returns 0 (see UartConsole), so this loop naturally does nothing and
  // BIOS writes zero bytes to the physical UART - no extra "is transport
  // active?" branching needed here.
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
    case Command::kTransportStatus:
      printTransportStatus();
      return;
    case Command::kTransportStart:
      handleTransportStart();
      return;
    case Command::kTransportStop:
      handleTransportStop();
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
  writeLine(console_, "  help             - show this message");
  writeLine(console_, "  status           - show BIOS/transport status");
  writeLine(console_, "  reboot           - restart the device");
  writeLine(console_, "  transport status - show console/transport state only");
  writeLine(console_, "  transport start  - hand UART to Transport (console goes mute)");
  writeLine(console_, "  transport stop   - return UART to the console");
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
            static_cast<unsigned long>(grut::kPhysicalUartBaud));
  writeLine(console_, buf);

  snprintf(buf, sizeof(buf), "free_heap_bytes=%lu",
            static_cast<unsigned long>(ESP.getFreeHeap()));
  writeLine(console_, buf);

  writeLine(console_,
            console_.isRunning() ? "console=running" : "console=stopped");

  snprintf(buf, sizeof(buf), "transport_name=%s", transport_.name());
  writeLine(console_, buf);

  writeLine(console_, transport_.isRunning() ? "transport=running"
                                              : "transport=stopped");
}

void Bios::printTransportStatus() {
  // Read-only: never changes ownership, just reports current state.
  writeLine(console_,
            console_.isRunning() ? "console=running" : "console=stopped");
  writeLine(console_, transport_.isRunning() ? "transport=running"
                                              : "transport=stopped");
}

void Bios::handleTransportStart() {
  // RuntimeManager::enableTransport() stops the console before starting
  // Transport. If it succeeds, console_.write() below (and everywhere
  // else in BIOS) becomes a silent no-op - see IConsole/UartConsole -
  // so BIOS structurally writes no further bytes to Serial, without any
  // extra "is transport active?" branching here.
  if (!runtimeManager_.enableTransport()) {
    // Rollback already happened inside enableTransport(): the console
    // was restarted, so this write reaches the console normally.
    writeLine(console_, "transport_start=failed");
    writeLine(console_, "console=running");
  }
}

void Bios::handleTransportStop() {
  // RuntimeManager::disableTransport() stops Transport, then starts the
  // console - by the time it returns, the console already owns Serial
  // again, so this confirmation is only ever printed after that handoff
  // completed.
  runtimeManager_.disableTransport();
  writeLine(console_, "transport_stop=ok");
  writeLine(console_,
            console_.isRunning() ? "console=running" : "console=stopped");
}

void Bios::reboot() {
  writeLine(console_, "rebooting...");
  console_.flush();
  delay(100);
  ESP.restart();
}

}  // namespace bios
}  // namespace grut
