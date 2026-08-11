#include <unity.h>

#include "LinkManager.h"

using grut::link::LinkManager;
using grut::link::LinkState;

void setUp() {}
void tearDown() {}

void test_starts_unknown_until_first_heartbeat() {
  LinkManager lm;
  lm.poll(5000);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LinkState::kUnknown),
                          static_cast<uint8_t>(lm.state()));
  TEST_ASSERT_EQUAL_UINT32(LinkManager::kNoHeartbeatAgeMs,
                           lm.heartbeatAgeMs(5000));
}

void test_first_heartbeat_moves_unknown_to_up() {
  LinkManager lm;
  lm.onHeartbeat(100);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LinkState::kUp),
                          static_cast<uint8_t>(lm.state()));
  TEST_ASSERT_EQUAL_UINT32(0, lm.heartbeatAgeMs(100));
}

void test_freshness_transitions_up_degraded_down() {
  LinkManager lm;
  lm.onHeartbeat(1000);

  lm.poll(3499);  // age 2499
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LinkState::kUp),
                          static_cast<uint8_t>(lm.state()));

  lm.poll(3500);  // age 2500: near-timeout band is degraded
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LinkState::kDegraded),
                          static_cast<uint8_t>(lm.state()));

  lm.poll(3999);  // age 2999
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LinkState::kDegraded),
                          static_cast<uint8_t>(lm.state()));

  lm.poll(4000);  // age 3000
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LinkState::kDown),
                          static_cast<uint8_t>(lm.state()));
}

void test_short_window_loss_above_five_percent_degrades_link() {
  LinkManager lm;
  lm.onHeartbeat(0);
  lm.onFrameReceived(0, 100);
  lm.onFrameReceived(2, 100);  // one missing frame: 1 / (2 + 1) = 33.3%
  lm.poll(100);

  TEST_ASSERT_TRUE(lm.shortLossPermille(100) >
                   LinkManager::kDegradedLossPermille);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LinkState::kDegraded),
                          static_cast<uint8_t>(lm.state()));
}

void test_exactly_five_percent_loss_does_not_degrade() {
  LinkManager lm;
  lm.onHeartbeat(0);
  lm.onFrameReceived(0, 100);
  lm.onFrameReceived(2, 100);  // one gap
  for (uint16_t seq = 3; seq <= 19; ++seq) {
    lm.onFrameReceived(seq, 100);
  }
  // 19 received + 1 gap = 20 expected => exactly 5.0%.
  TEST_ASSERT_EQUAL_UINT16(50, lm.shortLossPermille(100));
  lm.poll(100);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LinkState::kUp),
                          static_cast<uint8_t>(lm.state()));
}

void test_short_loss_ages_out_after_ten_seconds() {
  LinkManager lm;
  lm.onHeartbeat(0);
  lm.onFrameReceived(0, 100);
  lm.onFrameReceived(2, 100);  // creates high short-window loss
  lm.poll(100);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LinkState::kDegraded),
                          static_cast<uint8_t>(lm.state()));

  // Keep heartbeat healthy while the old loss bucket expires.
  lm.onHeartbeat(10000);
  lm.poll(10000);
  TEST_ASSERT_EQUAL_UINT16(0, lm.shortLossPermille(10000));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LinkState::kUp),
                          static_cast<uint8_t>(lm.state()));
}

void test_sequential_sequence_wrap_has_no_gap() {
  LinkManager lm;
  lm.onFrameReceived(65534, 0);
  lm.onFrameReceived(65535, 1);
  lm.onFrameReceived(0, 2);
  lm.onFrameReceived(1, 3);

  TEST_ASSERT_EQUAL_UINT32(4, lm.receivedFrameCount());
  TEST_ASSERT_EQUAL_UINT32(0, lm.sequenceGapCount());
  TEST_ASSERT_EQUAL_UINT16(0, lm.longLossPermille());
}

void test_gap_across_sequence_wrap_is_counted() {
  LinkManager lm;
  lm.onFrameReceived(65534, 0);
  lm.onFrameReceived(1, 1);  // missing 65535 and 0

  TEST_ASSERT_EQUAL_UINT32(2, lm.receivedFrameCount());
  TEST_ASSERT_EQUAL_UINT32(2, lm.sequenceGapCount());
  TEST_ASSERT_EQUAL_UINT16(500, lm.longLossPermille());
}

void test_duplicate_or_late_frame_does_not_create_huge_false_gap() {
  LinkManager lm;
  lm.onFrameReceived(10, 0);
  lm.onFrameReceived(11, 1);
  lm.onFrameReceived(10, 2);  // late/duplicate
  lm.onFrameReceived(12, 3);

  TEST_ASSERT_EQUAL_UINT32(4, lm.receivedFrameCount());
  TEST_ASSERT_EQUAL_UINT32(0, lm.sequenceGapCount());
}

void test_down_then_heartbeat_enters_recovering() {
  LinkManager lm;
  lm.onHeartbeat(0);
  lm.poll(3000);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LinkState::kDown),
                          static_cast<uint8_t>(lm.state()));

  lm.onHeartbeat(3100);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LinkState::kRecovering),
                          static_cast<uint8_t>(lm.state()));
  TEST_ASSERT_EQUAL_UINT8(1, lm.recoveryHeartbeatCount());
}

void test_three_recovery_heartbeats_return_link_to_up() {
  LinkManager lm;
  lm.onHeartbeat(0);
  lm.poll(3000);
  lm.onHeartbeat(3100);
  lm.onHeartbeat(4100);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LinkState::kRecovering),
                          static_cast<uint8_t>(lm.state()));
  TEST_ASSERT_EQUAL_UINT8(2, lm.recoveryHeartbeatCount());

  lm.onHeartbeat(5100);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LinkState::kUp),
                          static_cast<uint8_t>(lm.state()));
  TEST_ASSERT_EQUAL_UINT8(0, lm.recoveryHeartbeatCount());
}

void test_recovering_times_out_back_to_down() {
  LinkManager lm;
  lm.onHeartbeat(0);
  lm.poll(3000);
  lm.onHeartbeat(3100);  // recovering, count 1
  lm.poll(6100);         // 3000 ms since that heartbeat

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LinkState::kDown),
                          static_cast<uint8_t>(lm.state()));
  TEST_ASSERT_EQUAL_UINT8(0, lm.recoveryHeartbeatCount());
}

void test_heartbeat_age_handles_millis_wraparound() {
  LinkManager lm;
  lm.onHeartbeat(0xFFFFFF00u);
  // 0xFFFFFF00 -> wrap -> 0x00000100 is 512 ms elapsed.
  lm.poll(0x00000100u);
  TEST_ASSERT_EQUAL_UINT32(512, lm.heartbeatAgeMs(0x00000100u));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LinkState::kUp),
                          static_cast<uint8_t>(lm.state()));
}

void test_operational_counters_are_exposed_in_snapshot() {
  LinkManager lm;
  lm.onHeartbeat(100);
  lm.onFrameReceived(7, 100);
  lm.onSendFailure();
  lm.onSendFailure(3);
  lm.onQueueDrop();
  lm.onQueueDrop(4);

  const auto stats = lm.snapshot(350);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LinkState::kUp),
                          static_cast<uint8_t>(stats.state));
  TEST_ASSERT_EQUAL_UINT32(250, stats.heartbeatAgeMs);
  TEST_ASSERT_EQUAL_UINT32(1, stats.receivedFrames);
  TEST_ASSERT_EQUAL_UINT32(0, stats.sequenceGaps);
  TEST_ASSERT_EQUAL_UINT32(4, stats.sendFailures);
  TEST_ASSERT_EQUAL_UINT32(5, stats.queueDrops);
}

void test_reset_clears_state_and_counters() {
  LinkManager lm;
  lm.onHeartbeat(0);
  lm.onFrameReceived(1, 0);
  lm.onSendFailure();
  lm.onQueueDrop();
  lm.reset();

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LinkState::kUnknown),
                          static_cast<uint8_t>(lm.state()));
  TEST_ASSERT_EQUAL_UINT32(0, lm.receivedFrameCount());
  TEST_ASSERT_EQUAL_UINT32(0, lm.sequenceGapCount());
  TEST_ASSERT_EQUAL_UINT32(0, lm.sendFailureCount());
  TEST_ASSERT_EQUAL_UINT32(0, lm.queueDropCount());
  TEST_ASSERT_EQUAL_UINT16(0, lm.shortLossPermille(0));
  TEST_ASSERT_EQUAL_UINT16(0, lm.longLossPermille());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_starts_unknown_until_first_heartbeat);
  RUN_TEST(test_first_heartbeat_moves_unknown_to_up);
  RUN_TEST(test_freshness_transitions_up_degraded_down);
  RUN_TEST(test_short_window_loss_above_five_percent_degrades_link);
  RUN_TEST(test_exactly_five_percent_loss_does_not_degrade);
  RUN_TEST(test_short_loss_ages_out_after_ten_seconds);
  RUN_TEST(test_sequential_sequence_wrap_has_no_gap);
  RUN_TEST(test_gap_across_sequence_wrap_is_counted);
  RUN_TEST(test_duplicate_or_late_frame_does_not_create_huge_false_gap);
  RUN_TEST(test_down_then_heartbeat_enters_recovering);
  RUN_TEST(test_three_recovery_heartbeats_return_link_to_up);
  RUN_TEST(test_recovering_times_out_back_to_down);
  RUN_TEST(test_heartbeat_age_handles_millis_wraparound);
  RUN_TEST(test_operational_counters_are_exposed_in_snapshot);
  RUN_TEST(test_reset_clears_state_and_counters);
  return UNITY_END();
}
