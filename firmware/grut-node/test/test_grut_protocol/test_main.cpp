#include <unity.h>

#include <cstring>
#include <vector>

#include "Crc16.h"
#include "FrameCodec.h"
#include "GrutProtocol.h"

using namespace grut::protocol;

void setUp() {}
void tearDown() {}

// --- CRC sanity (independent, well-known test vector) ---

void test_crc16_known_vector() {
  const uint8_t input[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  TEST_ASSERT_EQUAL_HEX16(0x29B1, crc16CcittFalse(input, sizeof(input)));
}

// --- valid frame encode/decode ---

void test_valid_frame_encode_decode_roundtrip() {
  GrutFrameHeader header;
  header.type = static_cast<uint8_t>(PacketType::kData);
  header.srcAddr = 0x01;
  header.dstAddr = 0x02;
  header.ttl = 5;
  header.sequence = 1234;

  const uint8_t payload[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03};
  uint8_t frame[kMaxFrameSizeBytes];

  const size_t frameLen =
      encodeFrame(header, payload, sizeof(payload), frame, sizeof(frame));
  TEST_ASSERT_TRUE(frameLen > 0);
  TEST_ASSERT_EQUAL(kHeaderWireSize + sizeof(payload) + kCrcWireSize,
                     frameLen);

  GrutFrameHeader decodedHeader;
  uint8_t decodedPayload[kMaxGrutPayloadBytes];
  size_t decodedPayloadLen = 0;

  const DecodeResult result =
      decodeFrame(frame, frameLen, &decodedHeader, decodedPayload,
                  sizeof(decodedPayload), &decodedPayloadLen);

  TEST_ASSERT_EQUAL(static_cast<int>(DecodeResult::kOk),
                     static_cast<int>(result));
  TEST_ASSERT_EQUAL(sizeof(payload), decodedPayloadLen);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, decodedPayload, sizeof(payload));
}

// --- sequence and addressing preservation ---

void test_sequence_and_addressing_are_preserved() {
  GrutFrameHeader header;
  header.type = static_cast<uint8_t>(PacketType::kHeartbeat);
  header.srcAddr = 0x2A;
  header.dstAddr = kBroadcastAddress;
  header.ttl = 3;
  header.sequence = 0xBEEF;
  header.flags = kFlagBroadcast;

  uint8_t frame[kMaxFrameSizeBytes];
  const size_t frameLen = encodeFrame(header, nullptr, 0, frame, sizeof(frame));
  TEST_ASSERT_TRUE(frameLen > 0);

  GrutFrameHeader decodedHeader;
  uint8_t decodedPayload[1];
  size_t decodedPayloadLen = 0;
  const DecodeResult result =
      decodeFrame(frame, frameLen, &decodedHeader, decodedPayload,
                  sizeof(decodedPayload), &decodedPayloadLen);

  TEST_ASSERT_EQUAL(static_cast<int>(DecodeResult::kOk),
                     static_cast<int>(result));
  TEST_ASSERT_EQUAL(0, decodedPayloadLen);
  TEST_ASSERT_EQUAL_UINT8(header.srcAddr, decodedHeader.srcAddr);
  TEST_ASSERT_EQUAL_UINT8(header.dstAddr, decodedHeader.dstAddr);
  TEST_ASSERT_EQUAL_UINT8(header.ttl, decodedHeader.ttl);
  TEST_ASSERT_EQUAL_UINT16(header.sequence, decodedHeader.sequence);
  TEST_ASSERT_EQUAL_UINT8(header.flags, decodedHeader.flags);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PacketType::kHeartbeat),
                           decodedHeader.type);
}

// --- CRC rejection ---

void test_crc_rejection_on_corrupted_payload() {
  GrutFrameHeader header;
  header.srcAddr = 1;
  header.dstAddr = 2;
  header.sequence = 7;

  const uint8_t payload[] = {0x01, 0x02, 0x03};
  uint8_t frame[kMaxFrameSizeBytes];
  const size_t frameLen =
      encodeFrame(header, payload, sizeof(payload), frame, sizeof(frame));
  TEST_ASSERT_TRUE(frameLen > 0);

  // Flip a bit in the payload without touching the CRC trailer.
  frame[kHeaderWireSize] ^= 0xFF;

  GrutFrameHeader decodedHeader;
  uint8_t decodedPayload[kMaxGrutPayloadBytes];
  size_t decodedPayloadLen = 0;
  const DecodeResult result =
      decodeFrame(frame, frameLen, &decodedHeader, decodedPayload,
                  sizeof(decodedPayload), &decodedPayloadLen);

  TEST_ASSERT_EQUAL(static_cast<int>(DecodeResult::kCrcMismatch),
                     static_cast<int>(result));
}

// --- wrong protocol version rejection ---

void test_wrong_protocol_version_is_rejected() {
  GrutFrameHeader header;
  header.srcAddr = 1;
  header.dstAddr = 2;

  uint8_t frame[kMaxFrameSizeBytes];
  const size_t frameLen = encodeFrame(header, nullptr, 0, frame, sizeof(frame));
  TEST_ASSERT_TRUE(frameLen > 0);

  // Corrupt only the version byte, then recompute the CRC so this test
  // exercises the version check specifically, not a CRC failure.
  frame[0] = kProtocolVersion + 1;
  const uint16_t fixedCrc = crc16CcittFalse(frame, kHeaderWireSize);
  frame[kHeaderWireSize] = static_cast<uint8_t>(fixedCrc & 0xFF);
  frame[kHeaderWireSize + 1] = static_cast<uint8_t>((fixedCrc >> 8) & 0xFF);

  GrutFrameHeader decodedHeader;
  uint8_t decodedPayload[1];
  size_t decodedPayloadLen = 0;
  const DecodeResult result =
      decodeFrame(frame, frameLen, &decodedHeader, decodedPayload,
                  sizeof(decodedPayload), &decodedPayloadLen);

  TEST_ASSERT_EQUAL(static_cast<int>(DecodeResult::kUnsupportedVersion),
                     static_cast<int>(result));
}

// --- length bounds ---

void test_too_short_buffer_is_rejected() {
  uint8_t tinyBuffer[3] = {1, 2, 3};
  GrutFrameHeader decodedHeader;
  uint8_t decodedPayload[1];
  size_t decodedPayloadLen = 0;
  const DecodeResult result =
      decodeFrame(tinyBuffer, sizeof(tinyBuffer), &decodedHeader,
                  decodedPayload, sizeof(decodedPayload), &decodedPayloadLen);
  TEST_ASSERT_EQUAL(static_cast<int>(DecodeResult::kTooShort),
                     static_cast<int>(result));
}

void test_too_long_buffer_is_rejected() {
  uint8_t oversized[kMaxFrameSizeBytes + 1] = {};
  GrutFrameHeader decodedHeader;
  uint8_t decodedPayload[1];
  size_t decodedPayloadLen = 0;
  const DecodeResult result =
      decodeFrame(oversized, sizeof(oversized), &decodedHeader,
                  decodedPayload, sizeof(decodedPayload), &decodedPayloadLen);
  TEST_ASSERT_EQUAL(static_cast<int>(DecodeResult::kTooLong),
                     static_cast<int>(result));
}

void test_length_field_mismatch_is_rejected() {
  GrutFrameHeader header;
  header.srcAddr = 1;
  header.dstAddr = 2;

  const uint8_t payload[] = {0x01, 0x02, 0x03, 0x04};
  uint8_t frame[kMaxFrameSizeBytes];
  const size_t frameLen =
      encodeFrame(header, payload, sizeof(payload), frame, sizeof(frame));
  TEST_ASSERT_TRUE(frameLen > 0);

  // Claim a different payload length in the header than the buffer
  // actually carries, without changing frameLen.
  frame[kHeaderWireSize - 1] = static_cast<uint8_t>(sizeof(payload) + 1);

  GrutFrameHeader decodedHeader;
  uint8_t decodedPayload[kMaxGrutPayloadBytes];
  size_t decodedPayloadLen = 0;
  const DecodeResult result =
      decodeFrame(frame, frameLen, &decodedHeader, decodedPayload,
                  sizeof(decodedPayload), &decodedPayloadLen);

  TEST_ASSERT_EQUAL(static_cast<int>(DecodeResult::kLengthMismatch),
                     static_cast<int>(result));
}

// --- maximum payload ---

void test_maximum_payload_is_accepted() {
  GrutFrameHeader header;
  header.srcAddr = 9;
  header.dstAddr = 10;

  std::vector<uint8_t> payload(kMaxGrutPayloadBytes, 0xAB);
  uint8_t frame[kMaxFrameSizeBytes];

  const size_t frameLen = encodeFrame(header, payload.data(), payload.size(),
                                       frame, sizeof(frame));
  TEST_ASSERT_EQUAL(kMaxFrameSizeBytes, frameLen);

  GrutFrameHeader decodedHeader;
  std::vector<uint8_t> decodedPayload(kMaxGrutPayloadBytes);
  size_t decodedPayloadLen = 0;
  const DecodeResult result =
      decodeFrame(frame, frameLen, &decodedHeader, decodedPayload.data(),
                  decodedPayload.size(), &decodedPayloadLen);

  TEST_ASSERT_EQUAL(static_cast<int>(DecodeResult::kOk),
                     static_cast<int>(result));
  TEST_ASSERT_EQUAL(kMaxGrutPayloadBytes, decodedPayloadLen);
}

void test_payload_over_maximum_is_rejected_by_encoder() {
  GrutFrameHeader header;
  std::vector<uint8_t> payload(kMaxGrutPayloadBytes + 1, 0xAB);
  uint8_t frame[kMaxFrameSizeBytes + 16];  // capacity is not the limiting factor

  const size_t frameLen = encodeFrame(header, payload.data(), payload.size(),
                                       frame, sizeof(frame));
  TEST_ASSERT_EQUAL(0, frameLen);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_crc16_known_vector);
  RUN_TEST(test_valid_frame_encode_decode_roundtrip);
  RUN_TEST(test_sequence_and_addressing_are_preserved);
  RUN_TEST(test_crc_rejection_on_corrupted_payload);
  RUN_TEST(test_wrong_protocol_version_is_rejected);
  RUN_TEST(test_too_short_buffer_is_rejected);
  RUN_TEST(test_too_long_buffer_is_rejected);
  RUN_TEST(test_length_field_mismatch_is_rejected);
  RUN_TEST(test_maximum_payload_is_accepted);
  RUN_TEST(test_payload_over_maximum_is_rejected_by_encoder);
  return UNITY_END();
}
