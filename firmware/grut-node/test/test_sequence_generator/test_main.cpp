#include <unity.h>

#include "transport/SequenceGenerator.h"

using grut::transport::SequenceGenerator;

void setUp() {}
void tearDown() {}

void test_sequence_starts_at_zero_and_increments() {
  SequenceGenerator sequence;

  TEST_ASSERT_EQUAL_UINT16(0, sequence.next());
  TEST_ASSERT_EQUAL_UINT16(1, sequence.next());
  TEST_ASSERT_EQUAL_UINT16(2, sequence.next());
}

void test_sequence_wraps_from_ffff_to_zero() {
  SequenceGenerator sequence(0xFFFE);

  TEST_ASSERT_EQUAL_UINT16(0xFFFE, sequence.next());
  TEST_ASSERT_EQUAL_UINT16(0xFFFF, sequence.next());
  TEST_ASSERT_EQUAL_UINT16(0x0000, sequence.next());
  TEST_ASSERT_EQUAL_UINT16(0x0001, sequence.next());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_sequence_starts_at_zero_and_increments);
  RUN_TEST(test_sequence_wraps_from_ffff_to_zero);
  return UNITY_END();
}