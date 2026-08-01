#include <unity.h>

#include "CommandParser.h"

using grut::bios::Command;
using grut::bios::parseCommandLine;

void setUp() {}
void tearDown() {}

void test_help_command() {
  TEST_ASSERT_EQUAL(static_cast<int>(Command::kHelp),
                     static_cast<int>(parseCommandLine("help")));
  TEST_ASSERT_EQUAL(static_cast<int>(Command::kHelp),
                     static_cast<int>(parseCommandLine("?")));
}

void test_help_is_case_insensitive_and_trims_whitespace() {
  TEST_ASSERT_EQUAL(static_cast<int>(Command::kHelp),
                     static_cast<int>(parseCommandLine("  HeLp  ")));
}

void test_status_command() {
  TEST_ASSERT_EQUAL(static_cast<int>(Command::kStatus),
                     static_cast<int>(parseCommandLine("status")));
  TEST_ASSERT_EQUAL(static_cast<int>(Command::kStatus),
                     static_cast<int>(parseCommandLine("STATUS")));
}

void test_reboot_command() {
  TEST_ASSERT_EQUAL(static_cast<int>(Command::kReboot),
                     static_cast<int>(parseCommandLine("reboot")));
  TEST_ASSERT_EQUAL(static_cast<int>(Command::kReboot),
                     static_cast<int>(parseCommandLine("  Reboot  ")));
}

void test_empty_line_is_ignored() {
  TEST_ASSERT_EQUAL(static_cast<int>(Command::kEmpty),
                     static_cast<int>(parseCommandLine("")));
  TEST_ASSERT_EQUAL(static_cast<int>(Command::kEmpty),
                     static_cast<int>(parseCommandLine("   ")));
}

void test_unknown_commands_are_rejected() {
  // These must stay unknown in v0.1: BIOS must not accept anything that
  // looks like role/mesh/transport control (out of scope this milestone).
  TEST_ASSERT_EQUAL(static_cast<int>(Command::kUnknown),
                     static_cast<int>(parseCommandLine("mesh")));
  TEST_ASSERT_EQUAL(static_cast<int>(Command::kUnknown),
                     static_cast<int>(parseCommandLine("role air")));
  TEST_ASSERT_EQUAL(static_cast<int>(Command::kUnknown),
                     static_cast<int>(parseCommandLine("wifi")));
}

void test_transport_subcommands() {
  TEST_ASSERT_EQUAL(static_cast<int>(Command::kTransportStatus),
                     static_cast<int>(parseCommandLine("transport status")));
  TEST_ASSERT_EQUAL(static_cast<int>(Command::kTransportStart),
                     static_cast<int>(parseCommandLine("transport start")));
  TEST_ASSERT_EQUAL(static_cast<int>(Command::kTransportStop),
                     static_cast<int>(parseCommandLine("transport stop")));
}

void test_transport_subcommands_are_case_insensitive_and_trim() {
  TEST_ASSERT_EQUAL(static_cast<int>(Command::kTransportStart),
                     static_cast<int>(parseCommandLine("  TRANSPORT START  ")));
}

void test_incomplete_or_unknown_transport_commands_are_rejected() {
  TEST_ASSERT_EQUAL(static_cast<int>(Command::kUnknown),
                     static_cast<int>(parseCommandLine("transport")));
  TEST_ASSERT_EQUAL(static_cast<int>(Command::kUnknown),
                     static_cast<int>(parseCommandLine("transport enable")));
}

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(test_help_command);
  RUN_TEST(test_help_is_case_insensitive_and_trims_whitespace);
  RUN_TEST(test_status_command);
  RUN_TEST(test_reboot_command);
  RUN_TEST(test_empty_line_is_ignored);
  RUN_TEST(test_unknown_commands_are_rejected);
  RUN_TEST(test_transport_subcommands);
  RUN_TEST(test_transport_subcommands_are_case_insensitive_and_trim);
  RUN_TEST(test_incomplete_or_unknown_transport_commands_are_rejected);
  return UNITY_END();
}
