#include <unity.h>

#include "LinkStatsCodec.h"

using grut::link::LinkState;
using grut::link::LinkStats;
using grut::link::decodeLinkStats;
using grut::link::encodeLinkStats;
using grut::link::kControlSubtypeLinkStats;
using grut::link::kLinkStatsPayloadSize;
using grut::link::kLinkStatsPayloadVersion;

void test_link_stats_roundtrip_preserves_all_fields() {
  LinkStats in;
  in.state = LinkState::kDegraded;
  in.heartbeatAgeMs = 0x12345678u;
  in.receivedFrames = 123456u;
  in.sequenceGaps = 789u;
  in.shortLossPermille = 57u;
  in.longLossPermille = 12u;
  in.sendFailures = 34u;
  in.queueDrops = 56u;
  in.recoveryHeartbeats = 2u;

  uint8_t payload[kLinkStatsPayloadSize] = {};
  TEST_ASSERT_EQUAL_UINT32(kLinkStatsPayloadSize,
                           encodeLinkStats(in, payload, sizeof(payload)));
  TEST_ASSERT_EQUAL_UINT8(kControlSubtypeLinkStats, payload[0]);
  TEST_ASSERT_EQUAL_UINT8(kLinkStatsPayloadVersion, payload[1]);

  LinkStats out;
  TEST_ASSERT_TRUE(decodeLinkStats(payload, sizeof(payload), &out));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(in.state),
                          static_cast<uint8_t>(out.state));
  TEST_ASSERT_EQUAL_UINT32(in.heartbeatAgeMs, out.heartbeatAgeMs);
  TEST_ASSERT_EQUAL_UINT32(in.receivedFrames, out.receivedFrames);
  TEST_ASSERT_EQUAL_UINT32(in.sequenceGaps, out.sequenceGaps);
  TEST_ASSERT_EQUAL_UINT16(in.shortLossPermille, out.shortLossPermille);
  TEST_ASSERT_EQUAL_UINT16(in.longLossPermille, out.longLossPermille);
  TEST_ASSERT_EQUAL_UINT32(in.sendFailures, out.sendFailures);
  TEST_ASSERT_EQUAL_UINT32(in.queueDrops, out.queueDrops);
  TEST_ASSERT_EQUAL_UINT8(in.recoveryHeartbeats, out.recoveryHeartbeats);
}

void test_encode_rejects_small_buffer() {
  LinkStats stats;
  uint8_t payload[kLinkStatsPayloadSize - 1] = {};
  TEST_ASSERT_EQUAL_UINT32(0, encodeLinkStats(stats, payload, sizeof(payload)));
}

void test_decode_rejects_wrong_length() {
  LinkStats stats;
  uint8_t payload[kLinkStatsPayloadSize] = {};
  TEST_ASSERT_EQUAL_UINT32(kLinkStatsPayloadSize,
                           encodeLinkStats(stats, payload, sizeof(payload)));
  LinkStats out;
  TEST_ASSERT_FALSE(decodeLinkStats(payload, sizeof(payload) - 1, &out));
}

void test_decode_rejects_unknown_subtype() {
  LinkStats stats;
  uint8_t payload[kLinkStatsPayloadSize] = {};
  encodeLinkStats(stats, payload, sizeof(payload));
  payload[0] = 0x7Fu;
  LinkStats out;
  TEST_ASSERT_FALSE(decodeLinkStats(payload, sizeof(payload), &out));
}

void test_decode_rejects_unknown_version() {
  LinkStats stats;
  uint8_t payload[kLinkStatsPayloadSize] = {};
  encodeLinkStats(stats, payload, sizeof(payload));
  payload[1] = static_cast<uint8_t>(kLinkStatsPayloadVersion + 1u);
  LinkStats out;
  TEST_ASSERT_FALSE(decodeLinkStats(payload, sizeof(payload), &out));
}

void test_decode_rejects_invalid_state() {
  LinkStats stats;
  uint8_t payload[kLinkStatsPayloadSize] = {};
  encodeLinkStats(stats, payload, sizeof(payload));
  payload[2] = 0xFFu;
  LinkStats out;
  TEST_ASSERT_FALSE(decodeLinkStats(payload, sizeof(payload), &out));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_link_stats_roundtrip_preserves_all_fields);
  RUN_TEST(test_encode_rejects_small_buffer);
  RUN_TEST(test_decode_rejects_wrong_length);
  RUN_TEST(test_decode_rejects_unknown_subtype);
  RUN_TEST(test_decode_rejects_unknown_version);
  RUN_TEST(test_decode_rejects_invalid_state);
  return UNITY_END();
}
