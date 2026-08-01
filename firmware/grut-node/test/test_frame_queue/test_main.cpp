#include <unity.h>

#include <cstring>

#include "FrameQueue.h"

using grut::transport::FrameQueue;

void setUp() {}
void tearDown() {}

void test_push_pop_preserves_fifo_order() {
  FrameQueue q;
  const uint8_t a[] = {1, 2, 3};
  const uint8_t b[] = {4, 5};
  const uint8_t c[] = {6};

  TEST_ASSERT_TRUE(q.push(a, sizeof(a)));
  TEST_ASSERT_TRUE(q.push(b, sizeof(b)));
  TEST_ASSERT_TRUE(q.push(c, sizeof(c)));
  TEST_ASSERT_EQUAL(3, q.size());

  uint8_t out[FrameQueue::kMaxFrameBytes];
  size_t len = 0;

  TEST_ASSERT_TRUE(q.pop(out, sizeof(out), &len));
  TEST_ASSERT_EQUAL(sizeof(a), len);
  TEST_ASSERT_EQUAL(0, memcmp(out, a, len));

  TEST_ASSERT_TRUE(q.pop(out, sizeof(out), &len));
  TEST_ASSERT_EQUAL(sizeof(b), len);
  TEST_ASSERT_EQUAL(0, memcmp(out, b, len));

  TEST_ASSERT_TRUE(q.pop(out, sizeof(out), &len));
  TEST_ASSERT_EQUAL(sizeof(c), len);
  TEST_ASSERT_EQUAL(0, memcmp(out, c, len));

  TEST_ASSERT_TRUE(q.empty());
}

void test_pop_on_empty_queue_fails() {
  FrameQueue q;
  uint8_t out[FrameQueue::kMaxFrameBytes];
  size_t len = 0;
  TEST_ASSERT_FALSE(q.pop(out, sizeof(out), &len));
}

void test_capacity_boundary_exactly_kDepth_frames() {
  FrameQueue q;
  const uint8_t frame[] = {0xAA};

  for (size_t i = 0; i < FrameQueue::kDepth; ++i) {
    TEST_ASSERT_TRUE(q.push(frame, sizeof(frame)));
  }
  TEST_ASSERT_TRUE(q.full());
  TEST_ASSERT_EQUAL(FrameQueue::kDepth, q.size());
  TEST_ASSERT_EQUAL(0, q.droppedCount());
}

void test_overflow_drops_newest_and_increments_counter() {
  FrameQueue q;
  const uint8_t frame[] = {0xAA};

  for (size_t i = 0; i < FrameQueue::kDepth; ++i) {
    TEST_ASSERT_TRUE(q.push(frame, sizeof(frame)));
  }

  // Queue is now full - the next push must be rejected, not evict the
  // oldest entry (drop-newest, ADR 0006).
  const uint8_t overflowFrame[] = {0xBB};
  TEST_ASSERT_FALSE(q.push(overflowFrame, sizeof(overflowFrame)));
  TEST_ASSERT_EQUAL(1, q.droppedCount());
  TEST_ASSERT_EQUAL(FrameQueue::kDepth, q.size());

  // The original oldest frame must still be the one that pops first.
  uint8_t out[FrameQueue::kMaxFrameBytes];
  size_t len = 0;
  TEST_ASSERT_TRUE(q.pop(out, sizeof(out), &len));
  TEST_ASSERT_EQUAL(1, len);
  TEST_ASSERT_EQUAL(0xAA, out[0]);
}

void test_oversized_frame_is_rejected() {
  FrameQueue q;
  uint8_t big[FrameQueue::kMaxFrameBytes + 1] = {};

  TEST_ASSERT_FALSE(q.push(big, sizeof(big)));
  TEST_ASSERT_EQUAL(1, q.droppedCount());
  TEST_ASSERT_TRUE(q.empty());
}

void test_queue_remains_usable_after_wraparound() {
  // Push and pop repeatedly past kDepth total operations to exercise
  // the ring buffer's wraparound (head_ index cycling).
  FrameQueue q;
  for (uint8_t i = 0; i < 10; ++i) {
    const uint8_t frame[] = {i};
    TEST_ASSERT_TRUE(q.push(frame, sizeof(frame)));

    uint8_t out[FrameQueue::kMaxFrameBytes];
    size_t len = 0;
    TEST_ASSERT_TRUE(q.pop(out, sizeof(out), &len));
    TEST_ASSERT_EQUAL(1, len);
    TEST_ASSERT_EQUAL(i, out[0]);
  }
  TEST_ASSERT_EQUAL(0, q.droppedCount());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_push_pop_preserves_fifo_order);
  RUN_TEST(test_pop_on_empty_queue_fails);
  RUN_TEST(test_capacity_boundary_exactly_kDepth_frames);
  RUN_TEST(test_overflow_drops_newest_and_increments_counter);
  RUN_TEST(test_oversized_frame_is_rejected);
  RUN_TEST(test_queue_remains_usable_after_wraparound);
  return UNITY_END();
}
