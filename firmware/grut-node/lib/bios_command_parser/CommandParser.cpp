#include "CommandParser.h"

#include <algorithm>
#include <cctype>

namespace grut {
namespace bios {

namespace {

std::string trimAndLower(const std::string& input) {
  size_t start = 0;
  size_t end = input.size();

  while (start < end &&
         std::isspace(static_cast<unsigned char>(input[start]))) {
    ++start;
  }

  while (end > start &&
         std::isspace(static_cast<unsigned char>(input[end - 1]))) {
    --end;
  }

  std::string result = input.substr(start, end - start);
  std::transform(result.begin(), result.end(), result.begin(),
                  [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                  });
  return result;
}

}  // namespace

Command parseCommandLine(const std::string& rawLine) {
  const std::string line = trimAndLower(rawLine);

  if (line.empty()) {
    return Command::kEmpty;
  }
  if (line == "help" || line == "?") {
    return Command::kHelp;
  }
  if (line == "status") {
    return Command::kStatus;
  }
  if (line == "reboot") {
    return Command::kReboot;
  }
  if (line == "transport status") {
    return Command::kTransportStatus;
  }
  if (line == "transport start") {
    return Command::kTransportStart;
  }
  if (line == "transport stop") {
    return Command::kTransportStop;
  }
  return Command::kUnknown;
}

}  // namespace bios
}  // namespace grut
