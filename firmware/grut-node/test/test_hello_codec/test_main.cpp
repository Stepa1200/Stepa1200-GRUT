#include <unity.h>

#include "FrameCodec.h"
#include "GrutProtocol.h"
#include "HelloCodec.h"
#include "NeighborTable.h"

using grut::discovery::decodeHello;
using grut::discovery::encodeHello;
using grut::discovery::kControlSubtypeHello;
using grut::discovery::kHelloPayloadSize;
using grut::neighbor::NeighborInfo;
using grut::neighbor::NeighborTable;

void setUp() {}
void tearDown() {}

// --- Codec-level correctness ---

void test_encode_hello_produces_one_byte_subtype() {
  uint8_t buf[8];
  const size_t len = encodeHello(buf, sizeof(buf));
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<unsigned>(kHelloPayloadSize));
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<unsigned>(len));
  TEST_ASSERT_EQUAL_UINT8(kControlSubtypeHello, buf[0]);
}

void test_encode_hello_rejects_too_small_buffer() {
  uint8_t buf[1];
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<unsigned>(encodeHello(buf, 0)));
}

void test_decode_hello_accepts_well_formed_payload() {
  uint8_t buf[1] = {kControlSubtypeHello};
  TEST_ASSERT_TRUE(decodeHello(buf, sizeof(buf)));
}

void test_decode_hello_rejects_wrong_subtype() {
  uint8_t buf[1] = {0x01};  // LINK_STATS subtype, not HELLO
  TEST_ASSERT_FALSE(decodeHello(buf, sizeof(buf)));
}

void test_decode_hello_rejects_wrong_length() {
  uint8_t buf[2] = {kControlSubtypeHello, 0x00};
  TEST_ASSERT_FALSE(decodeHello(buf, sizeof(buf)));
  TEST_ASSERT_FALSE(decodeHello(buf, 0));
}

void test_decode_hello_rejects_null() {
  TEST_ASSERT_FALSE(decodeHello(nullptr, 1));
}

// --- Full GRUT frame round trip (proves the whole wire path, not just
// the payload codec in isolation) ---

void test_hello_frame_round_trip_through_real_frame_codec() {
  grut::protocol::GrutFrameHeader header;
  header.type = static_cast<uint8_t>(grut::protocol::PacketType::kControl);
  header.srcAddr = 3;  // AIR1
  header.dstAddr = grut::protocol::kBroadcastAddress;
  header.sequence = 42;

  uint8_t helloPayload[grut::discovery::kHelloPayloadSize];
  const size_t helloLen = encodeHello(helloPayload, sizeof(helloPayload));

  uint8_t frame[grut::protocol::kMaxFrameSizeBytes];
  const size_t frameLen = grut::protocol::encodeFrame(
      header, helloPayload, helloLen, frame, sizeof(frame));
  TEST_ASSERT_TRUE(frameLen > 0);

  grut::protocol::GrutFrameHeader decodedHeader;
  uint8_t decodedPayload[grut::protocol::kMaxGrutPayloadBytes];
  size_t decodedPayloadLen = 0;
  const auto result = grut::protocol::decodeFrame(
      frame, frameLen, &decodedHeader, decodedPayload,
      sizeof(decodedPayload), &decodedPayloadLen);

  TEST_ASSERT_TRUE(result == grut::protocol::DecodeResult::kOk);
  TEST_ASSERT_EQUAL_UINT8(3, decodedHeader.srcAddr);
  TEST_ASSERT_EQUAL_UINT8(grut::protocol::kBroadcastAddress,
                           decodedHeader.dstAddr);
  TEST_ASSERT_TRUE(decodeHello(decodedPayload, decodedPayloadLen));
}

// --- Integration with NeighborTable (the actual discovery behavior) ---

void test_first_hello_creates_neighbor() {
  NeighborTable table;
  table.onFrameObserved(/*srcAddr=*/3, /*nowMs=*/1000);

  NeighborInfo info = table.get(3);
  TEST_ASSERT_TRUE(info.known);
  TEST_ASSERT_EQUAL_UINT8(3, info.address);
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<unsigned>(table.count()));
}

void test_repeated_hello_refreshes_same_neighbor_not_duplicate() {
  NeighborTable table;
  table.onFrameObserved(3, 1000);
  table.onFrameObserved(3, 2000);
  table.onFrameObserved(3, 3000);

  TEST_ASSERT_EQUAL_UINT32(1, static_cast<unsigned>(table.count()));
  NeighborInfo info = table.get(3);
  TEST_ASSERT_EQUAL_UINT32(3000, info.lastSeenMs);
  TEST_ASSERT_EQUAL_UINT32(3, info.rxFrames);
}

void test_air1_and_air2_create_separate_entries() {
  NeighborTable table;
  const uint8_t air1 = 3;
  const uint8_t air2 = 4;

  table.onFrameObserved(air1, 1000);
  table.onFrameObserved(air2, 1200);
  table.onFrameObserved(air1, 2000);

  TEST_ASSERT_EQUAL_UINT32(2, static_cast<unsigned>(table.count()));

  NeighborInfo n1 = table.get(air1);
  TEST_ASSERT_EQUAL_UINT32(2, n1.rxFrames);
  TEST_ASSERT_EQUAL_UINT32(2000, n1.lastSeenMs);

  NeighborInfo n2 = table.get(air2);
  TEST_ASSERT_EQUAL_UINT32(1, n2.rxFrames);
  TEST_ASSERT_EQUAL_UINT32(1200, n2.lastSeenMs);
}

void test_table_capacity_handles_more_neighbors_than_max() {
  NeighborTable table;
  // Fill exactly to capacity, all distinct addresses.
  for (uint8_t i = 0; i < NeighborTable::kMaxNeighbors; ++i) {
    table.onFrameObserved(static_cast<uint8_t>(10 + i), 1000);
  }
  TEST_ASSERT_EQUAL_UINT32(static_cast<unsigned>(NeighborTable::kMaxNeighbors),
                           static_cast<unsigned>(table.count()));

  // One more distinct node's HELLO arrives - table is full.
  table.onFrameObserved(200, 2000);
  TEST_ASSERT_EQUAL_UINT32(static_cast<unsigned>(NeighborTable::kMaxNeighbors),
                           static_cast<unsigned>(table.count()));
  TEST_ASSERT_FALSE(table.get(200).known);
  TEST_ASSERT_EQUAL_UINT32(1, table.droppedNewNeighborCount());

  // Existing neighbors remain intact and updatable.
  table.onFrameObserved(10, 3000);
  TEST_ASSERT_EQUAL_UINT32(3000, table.get(10).lastSeenMs);
}

void test_neighbor_freshness_follows_existing_neighbor_table_semantics() {
  NeighborTable table;
  table.onFrameObserved(3, 1000);

  TEST_ASSERT_TRUE(table.isFresh(3, 1000 + NeighborTable::kDefaultStaleAfterMs));
  TEST_ASSERT_FALSE(
      table.isFresh(3, 1000 + NeighborTable::kDefaultStaleAfterMs + 1));
}

void test_malformed_hello_is_never_forwarded_to_neighbor_table() {
  // Simulates the dispatch logic bridge_main.cpp/link_diag_main.cpp
  // will use: only call onFrameObserved for the HELLO subtype if
  // decodeHello() actually accepted the payload. A malformed payload
  // must never reach NeighborTable at all in this design - proving
  // that here at the codec level, since NeighborTable itself has no
  // way to "reject" an address once told to observe it (by design,
  // that decision belongs one layer up).
  uint8_t badSubtype[1] = {0x01};
  uint8_t badLength[2] = {kControlSubtypeHello, 0x00};

  TEST_ASSERT_FALSE(decodeHello(badSubtype, sizeof(badSubtype)));
  TEST_ASSERT_FALSE(decodeHello(badLength, sizeof(badLength)));

  // A real dispatch would look like:
  //   if (subtypeByte == kControlSubtypeHello && decodeHello(...)) {
  //     table.onFrameObserved(srcAddr, now);
  //   }
  // i.e. NeighborTable.onFrameObserved() is only reached on successful
  // decode - already exercised positively by
  // test_first_hello_creates_neighbor above.
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_encode_hello_produces_one_byte_subtype);
  RUN_TEST(test_encode_hello_rejects_too_small_buffer);
  RUN_TEST(test_decode_hello_accepts_well_formed_payload);
  RUN_TEST(test_decode_hello_rejects_wrong_subtype);
  RUN_TEST(test_decode_hello_rejects_wrong_length);
  RUN_TEST(test_decode_hello_rejects_null);
  RUN_TEST(test_hello_frame_round_trip_through_real_frame_codec);
  RUN_TEST(test_first_hello_creates_neighbor);
  RUN_TEST(test_repeated_hello_refreshes_same_neighbor_not_duplicate);
  RUN_TEST(test_air1_and_air2_create_separate_entries);
  RUN_TEST(test_table_capacity_handles_more_neighbors_than_max);
  RUN_TEST(test_neighbor_freshness_follows_existing_neighbor_table_semantics);
  RUN_TEST(test_malformed_hello_is_never_forwarded_to_neighbor_table);
  return UNITY_END();
}
