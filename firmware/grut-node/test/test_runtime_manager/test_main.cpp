#include <unity.h>

#include <string>
#include <vector>

#include "RuntimeManager.h"

using grut::bios::IConsole;
using grut::bios::RuntimeManager;
using grut::transport::ITransport;

namespace {

std::vector<std::string>* g_log = nullptr;

// Console that starts running (matches real boot state: BIOS starts the
// console at begin(), Transport stays stopped).
class FakeConsole : public IConsole {
 public:
  bool start() override {
    g_log->push_back("console.start");
    running_ = true;
    return true;
  }
  void stop() override {
    g_log->push_back("console.stop");
    running_ = false;
  }
  bool isRunning() const override { return running_; }
  void poll() override {}
  int available() override { return 0; }
  int read() override { return -1; }
  size_t write(const uint8_t*, size_t length) override { return length; }
  void flush() override {}

 private:
  bool running_ = true;
};

class FakeTransport : public ITransport {
 public:
  explicit FakeTransport(bool shouldStartSucceed)
      : shouldStartSucceed_(shouldStartSucceed) {}

  bool start() override {
    g_log->push_back("transport.start");
    if (shouldStartSucceed_) {
      running_ = true;
    }
    return shouldStartSucceed_;
  }
  void stop() override {
    g_log->push_back("transport.stop");
    running_ = false;
  }
  bool isRunning() const override { return running_; }
  void poll() override {}
  bool send(const uint8_t*, size_t) override { return running_; }
  const char* name() const override { return "fake-uart"; }

 private:
  bool shouldStartSucceed_;
  bool running_ = false;
};

}  // namespace

void setUp() {}
void tearDown() {}

void test_enable_transport_stops_console_before_starting_transport() {
  std::vector<std::string> log;
  g_log = &log;

  FakeConsole console;
  FakeTransport transport(/*shouldStartSucceed=*/true);
  RuntimeManager manager(console, transport);

  const bool ok = manager.enableTransport();

  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL(2, static_cast<int>(log.size()));
  TEST_ASSERT_EQUAL_STRING("console.stop", log[0].c_str());
  TEST_ASSERT_EQUAL_STRING("transport.start", log[1].c_str());
  TEST_ASSERT_FALSE(manager.consoleRunning());
  TEST_ASSERT_TRUE(manager.transportRunning());
}

void test_disable_transport_stops_transport_before_starting_console() {
  std::vector<std::string> log;
  g_log = &log;

  FakeConsole console;
  FakeTransport transport(/*shouldStartSucceed=*/true);
  RuntimeManager manager(console, transport);

  manager.enableTransport();
  log.clear();

  manager.disableTransport();

  TEST_ASSERT_EQUAL(2, static_cast<int>(log.size()));
  TEST_ASSERT_EQUAL_STRING("transport.stop", log[0].c_str());
  TEST_ASSERT_EQUAL_STRING("console.start", log[1].c_str());
  TEST_ASSERT_TRUE(manager.consoleRunning());
  TEST_ASSERT_FALSE(manager.transportRunning());
}

void test_failed_transport_start_rolls_back_to_console() {
  std::vector<std::string> log;
  g_log = &log;

  FakeConsole console;
  FakeTransport transport(/*shouldStartSucceed=*/false);
  RuntimeManager manager(console, transport);

  const bool ok = manager.enableTransport();

  TEST_ASSERT_FALSE(ok);
  TEST_ASSERT_EQUAL(3, static_cast<int>(log.size()));
  TEST_ASSERT_EQUAL_STRING("console.stop", log[0].c_str());
  TEST_ASSERT_EQUAL_STRING("transport.start", log[1].c_str());
  TEST_ASSERT_EQUAL_STRING("console.start", log[2].c_str());
  TEST_ASSERT_TRUE(manager.consoleRunning());
  TEST_ASSERT_FALSE(manager.transportRunning());
}

void test_never_both_running_at_once_across_full_cycle() {
  std::vector<std::string> log;
  g_log = &log;

  FakeConsole console;
  FakeTransport transport(/*shouldStartSucceed=*/true);
  RuntimeManager manager(console, transport);

  TEST_ASSERT_TRUE(manager.consoleRunning());
  TEST_ASSERT_FALSE(manager.transportRunning());

  manager.enableTransport();
  TEST_ASSERT_FALSE(manager.consoleRunning() && manager.transportRunning());
  TEST_ASSERT_TRUE(manager.transportRunning());

  manager.disableTransport();
  TEST_ASSERT_FALSE(manager.consoleRunning() && manager.transportRunning());
  TEST_ASSERT_TRUE(manager.consoleRunning());
}

void test_repeated_enable_transport_is_safe() {
  std::vector<std::string> log;
  g_log = &log;

  FakeConsole console;
  FakeTransport transport(/*shouldStartSucceed=*/true);
  RuntimeManager manager(console, transport);

  const bool first = manager.enableTransport();
  const bool second = manager.enableTransport();

  TEST_ASSERT_TRUE(first);
  TEST_ASSERT_TRUE(second);
  TEST_ASSERT_FALSE(manager.consoleRunning());
  TEST_ASSERT_TRUE(manager.transportRunning());
  // Console never re-appears mid-sequence: every log entry before the
  // final state is still stop/start in the correct order, never two
  // starts of the same resource back to back without a stop between.
  for (size_t i = 0; i + 1 < log.size(); ++i) {
    const bool bothStarts =
        log[i].find(".start") != std::string::npos &&
        log[i + 1].find(".start") != std::string::npos &&
        log[i].substr(0, log[i].find('.')) ==
            log[i + 1].substr(0, log[i + 1].find('.'));
    TEST_ASSERT_FALSE(bothStarts);
  }
}

void test_repeated_disable_transport_is_safe() {
  std::vector<std::string> log;
  g_log = &log;

  FakeConsole console;
  FakeTransport transport(/*shouldStartSucceed=*/true);
  RuntimeManager manager(console, transport);

  manager.enableTransport();
  manager.disableTransport();
  manager.disableTransport();  // second call must not crash or misbehave

  TEST_ASSERT_TRUE(manager.consoleRunning());
  TEST_ASSERT_FALSE(manager.transportRunning());
}

void test_start_stop_start_cycle_ends_with_console_owning_uart() {
  std::vector<std::string> log;
  g_log = &log;

  FakeConsole console;
  FakeTransport transport(/*shouldStartSucceed=*/true);
  RuntimeManager manager(console, transport);

  manager.enableTransport();
  manager.disableTransport();
  manager.enableTransport();
  manager.disableTransport();

  TEST_ASSERT_TRUE(manager.consoleRunning());
  TEST_ASSERT_FALSE(manager.transportRunning());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_enable_transport_stops_console_before_starting_transport);
  RUN_TEST(test_disable_transport_stops_transport_before_starting_console);
  RUN_TEST(test_failed_transport_start_rolls_back_to_console);
  RUN_TEST(test_never_both_running_at_once_across_full_cycle);
  RUN_TEST(test_repeated_enable_transport_is_safe);
  RUN_TEST(test_repeated_disable_transport_is_safe);
  RUN_TEST(test_start_stop_start_cycle_ends_with_console_owning_uart);
  return UNITY_END();
}
