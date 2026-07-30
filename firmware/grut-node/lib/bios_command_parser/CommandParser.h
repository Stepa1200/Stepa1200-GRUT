#pragma once

#include <string>

namespace grut {
namespace bios {

enum class Command {
  kHelp,
  kStatus,
  kReboot,
  kEmpty,
  kUnknown,
};

// Parses a single console line (already received over UART) into a
// Command. Case-insensitive, trims leading/trailing whitespace.
//
// This function has no Arduino/ESP8266 dependency on purpose: it is the
// one piece of BIOS console logic that is practical to unit-test on the
// host (see firmware/grut-node/test/test_command_parser), independent of
// any ESP8266 toolchain or hardware.
Command parseCommandLine(const std::string& rawLine);

}  // namespace bios
}  // namespace grut
