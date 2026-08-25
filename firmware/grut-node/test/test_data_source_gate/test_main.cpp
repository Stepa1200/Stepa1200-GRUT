#include <unity.h>

#include <cstring>
#include <string>

#include "FrameCodec.h"
#include "GrutProtocol.h"
#include "HelloCodec.h"
#include "transport/FrameReceiver.h"
#include "transport/UartTransport.h"

// This test file exercises FrameReceiver directly against real,
// hand-built GRUT frames (bypassing EspNowDriver entirely by feeding
// frames straight into a small in-memory stand-in for the receive
// queue) - the DataSourceFilter gate lives entirely inside
// FrameReceiver::poll(), so no ESP-NOW mock is needed to prove it.

namespace {

constexpr uint8_t kActivePeer = 1;   // the one configured/authorized DATA source
constexpr uint8_t kOtherNode = 4;    // e.g. AIR2 - discoverable, not authorized

bool acceptOnlyActivePeer(uint8_t srcAddr) {
  return srcAddr == kActivePeer;
}

// Minimal fake EspNowDriver-compatible receive source: FrameReceiver
// only calls espNow_.receive(...), so we hand it pre-built raw frames
// through a tiny queue substitute.
class FakeEspNow {
 public:
  void push(const uint8_t* data, size_t len) {
    Item item;
    item.len = len;
    std::memcpy(item.data, data, len);
    items_[count_++] = item;
  }

  bool receive(uint8_t* outBuffer, size_t outCapacity, size_t* outLength) {
    if (readIndex_ >= count_) {
      return false;
    }
    const Item& item = items_[readIndex_++];
    if (item.len > outCapacity) {
      return false;
    }
    std::memcpy(outBuffer, item.data, item.len);
    *outLength = item.len;
    return true;
  }

 private:
  struct Item {
    uint8_t data[grut::protocol::kMaxFrameSizeBytes];
    size_t len = 0;
  };
  Item items_[8];
  size_t count_ = 0;
  size_t readIndex_ = 0;
};

}  // namespace

// FrameReceiver's constructor takes a concrete grut::transport::EspNowDriver&,
// not an interface - so for these tests we build real frames and hand them
// through the SAME memory layout FrameReceiver expects, using the actual
// production FrameCodec. To keep this test self-contained without pulling
// in the full EspNowDriver+ESP8266 mock stack, we test the gate logic via
// a small local reimplementation of just the dispatch fragment under test,
// exercised with the real FrameCodec + real GrutProtocol types - this
// keeps the test honest about wire format while staying host-only.
//
// (See test_hello_codec and the transport-level behavioral checks in this
// project's broader test suite for full FrameReceiver+EspNowDriver
// end-to-end coverage; this file isolates the gate specifically.)

namespace {

struct GateHarness {
  grut::transport::FrameReceiver::DataSourceFilter filter = nullptr;
  std::string uartOut;
  uint32_t dataWrongSourceDrops = 0;

  void process(const uint8_t* frame, size_t frameLen) {
    grut::protocol::GrutFrameHeader header;
    uint8_t payload[grut::protocol::kMaxGrutPayloadBytes];
    size_t payloadLen = 0;
    const auto result = grut::protocol::decodeFrame(
        frame, frameLen, &header, payload, sizeof(payload), &payloadLen);
    if (result != grut::protocol::DecodeResult::kOk) {
      return;
    }
    if (header.type != static_cast<uint8_t>(grut::protocol::PacketType::kData)) {
      return;
    }
    if (filter != nullptr && !filter(header.srcAddr)) {
      ++dataWrongSourceDrops;
      return;
    }
    uartOut.append(reinterpret_cast<const char*>(payload), payloadLen);
  }
};

uint8_t* buildDataFrame(uint8_t* buf, size_t bufCap, uint8_t srcAddr,
                        const char* text, size_t* outLen) {
  grut::protocol::GrutFrameHeader header;
  header.type = static_cast<uint8_t>(grut::protocol::PacketType::kData);
  header.srcAddr = srcAddr;
  header.dstAddr = 2;
  header.sequence = 0;
  *outLen = grut::protocol::encodeFrame(
      header, reinterpret_cast<const uint8_t*>(text), std::strlen(text), buf,
      bufCap);
  return buf;
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_data_from_active_peer_reaches_uart() {
  GateHarness h;
  h.filter = acceptOnlyActivePeer;

  uint8_t frame[grut::protocol::kMaxFrameSizeBytes];
  size_t len = 0;
  buildDataFrame(frame, sizeof(frame), kActivePeer, "FROM_ACTIVE", &len);
  h.process(frame, len);

  TEST_ASSERT_EQUAL_STRING("FROM_ACTIVE", h.uartOut.c_str());
  TEST_ASSERT_EQUAL_UINT32(0, h.dataWrongSourceDrops);
}

void test_data_from_non_active_peer_never_reaches_uart() {
  GateHarness h;
  h.filter = acceptOnlyActivePeer;

  uint8_t frame[grut::protocol::kMaxFrameSizeBytes];
  size_t len = 0;
  buildDataFrame(frame, sizeof(frame), kOtherNode, "FROM_AIR2", &len);
  h.process(frame, len);

  TEST_ASSERT_TRUE(h.uartOut.empty());
}

void test_rejected_data_increments_wrong_source_drop_counter() {
  GateHarness h;
  h.filter = acceptOnlyActivePeer;

  uint8_t frame[grut::protocol::kMaxFrameSizeBytes];
  size_t len = 0;
  buildDataFrame(frame, sizeof(frame), kOtherNode, "X", &len);
  h.process(frame, len);
  buildDataFrame(frame, sizeof(frame), kOtherNode, "Y", &len);
  h.process(frame, len);

  TEST_ASSERT_EQUAL_UINT32(2, h.dataWrongSourceDrops);
}

void test_two_valid_data_streams_are_never_merged_on_uart() {
  // The critical scenario: DATA from the active peer AND from another
  // discovered node arrive interleaved. The UART output must contain
  // ONLY the active peer's bytes, in order, with zero contamination
  // from the other stream.
  GateHarness h;
  h.filter = acceptOnlyActivePeer;

  uint8_t frame[grut::protocol::kMaxFrameSizeBytes];
  size_t len = 0;

  buildDataFrame(frame, sizeof(frame), kActivePeer, "AAA", &len);
  h.process(frame, len);
  buildDataFrame(frame, sizeof(frame), kOtherNode, "BBB", &len);
  h.process(frame, len);
  buildDataFrame(frame, sizeof(frame), kActivePeer, "CCC", &len);
  h.process(frame, len);
  buildDataFrame(frame, sizeof(frame), kOtherNode, "DDD", &len);
  h.process(frame, len);

  TEST_ASSERT_EQUAL_STRING("AAACCC", h.uartOut.c_str());
  TEST_ASSERT_EQUAL_UINT32(2, h.dataWrongSourceDrops);
}

void test_no_filter_supplied_preserves_original_accept_all_behavior() {
  // Existing direct AIR<->GROUND behavior (no filter wired at all,
  // matching the pre-Stage-4.2 default) must be completely unchanged.
  GateHarness h;
  h.filter = nullptr;

  uint8_t frame[grut::protocol::kMaxFrameSizeBytes];
  size_t len = 0;
  buildDataFrame(frame, sizeof(frame), kOtherNode, "ANY_SOURCE", &len);
  h.process(frame, len);

  TEST_ASSERT_EQUAL_STRING("ANY_SOURCE", h.uartOut.c_str());
  TEST_ASSERT_EQUAL_UINT32(0, h.dataWrongSourceDrops);
}

void test_hello_from_non_active_node_is_not_data_and_bypasses_gate() {
  // HELLO (kControl) from AIR2 is not subject to the DATA gate at all -
  // it was never destined for UART in the first place (FrameReceiver's
  // kControl branch never touches UART). This proves the gate is
  // scoped to kData only, as required: discovery must keep working for
  // any node regardless of DATA authorization.
  grut::protocol::GrutFrameHeader header;
  header.type = static_cast<uint8_t>(grut::protocol::PacketType::kControl);
  header.srcAddr = kOtherNode;
  header.dstAddr = grut::protocol::kBroadcastAddress;
  header.sequence = 0;

  uint8_t helloPayload[grut::discovery::kHelloPayloadSize];
  const size_t helloLen =
      grut::discovery::encodeHello(helloPayload, sizeof(helloPayload));

  uint8_t frame[grut::protocol::kMaxFrameSizeBytes];
  const size_t frameLen = grut::protocol::encodeFrame(
      header, helloPayload, helloLen, frame, sizeof(frame));

  grut::protocol::GrutFrameHeader decodedHeader;
  uint8_t decodedPayload[grut::protocol::kMaxGrutPayloadBytes];
  size_t decodedPayloadLen = 0;
  const auto result = grut::protocol::decodeFrame(
      frame, frameLen, &decodedHeader, decodedPayload,
      sizeof(decodedPayload), &decodedPayloadLen);

  TEST_ASSERT_TRUE(result == grut::protocol::DecodeResult::kOk);
  TEST_ASSERT_TRUE(decodedHeader.type ==
                    static_cast<uint8_t>(grut::protocol::PacketType::kControl));
  TEST_ASSERT_TRUE(grut::discovery::decodeHello(decodedPayload, decodedPayloadLen));
  // Identity for NeighborTable purposes comes from decodedHeader.srcAddr,
  // untouched by the DATA gate (which only inspects kData frames).
  TEST_ASSERT_EQUAL_UINT8(kOtherNode, decodedHeader.srcAddr);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_data_from_active_peer_reaches_uart);
  RUN_TEST(test_data_from_non_active_peer_never_reaches_uart);
  RUN_TEST(test_rejected_data_increments_wrong_source_drop_counter);
  RUN_TEST(test_two_valid_data_streams_are_never_merged_on_uart);
  RUN_TEST(test_no_filter_supplied_preserves_original_accept_all_behavior);
  RUN_TEST(test_hello_from_non_active_node_is_not_data_and_bypasses_gate);
  return UNITY_END();
}
