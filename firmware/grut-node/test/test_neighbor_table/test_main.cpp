#include <unity.h>

#include "NeighborTable.h"

using grut::neighbor::NeighborInfo;
using grut::neighbor::NeighborTable;

void setUp() {}
void tearDown() {}

void test_unknown_address_returns_default_info() {
  NeighborTable table;
  NeighborInfo info = table.get(5);
  TEST_ASSERT_FALSE(info.known);
  TEST_ASSERT_EQUAL_UINT32(0, table.count());
}

void test_first_observation_creates_entry() {
  NeighborTable table;
  table.onFrameObserved(1, 1000);

  NeighborInfo info = table.get(1);
  TEST_ASSERT_TRUE(info.known);
  TEST_ASSERT_EQUAL_UINT8(1, info.address);
  TEST_ASSERT_EQUAL_UINT32(1000, info.lastSeenMs);
  TEST_ASSERT_EQUAL_UINT32(1, info.rxFrames);
  TEST_ASSERT_EQUAL_UINT32(0, info.sequenceGaps);
  TEST_ASSERT_EQUAL_UINT32(1, table.count());
}

void test_repeated_observation_updates_existing_entry_not_duplicate() {
  NeighborTable table;
  table.onFrameObserved(1, 1000);
  table.onFrameObserved(1, 2000);
  table.onFrameObserved(1, 3000);

  TEST_ASSERT_EQUAL_UINT32(1, table.count());
  NeighborInfo info = table.get(1);
  TEST_ASSERT_EQUAL_UINT32(3000, info.lastSeenMs);
  TEST_ASSERT_EQUAL_UINT32(3, info.rxFrames);
}

void test_gap_count_accumulates_across_observations() {
  NeighborTable table;
  table.onFrameObserved(1, 1000, /*gapCount=*/0);
  table.onFrameObserved(1, 2000, /*gapCount=*/3);
  table.onFrameObserved(1, 3000, /*gapCount=*/1);

  NeighborInfo info = table.get(1);
  TEST_ASSERT_EQUAL_UINT32(4, info.sequenceGaps);
}

void test_multiple_distinct_neighbors_tracked_independently() {
  NeighborTable table;
  table.onFrameObserved(1, 1000);
  table.onFrameObserved(2, 1500);
  table.onFrameObserved(1, 2000);

  TEST_ASSERT_EQUAL_UINT32(2, table.count());

  NeighborInfo n1 = table.get(1);
  TEST_ASSERT_EQUAL_UINT32(2, n1.rxFrames);
  TEST_ASSERT_EQUAL_UINT32(2000, n1.lastSeenMs);

  NeighborInfo n2 = table.get(2);
  TEST_ASSERT_EQUAL_UINT32(1, n2.rxFrames);
  TEST_ASSERT_EQUAL_UINT32(1500, n2.lastSeenMs);
}

// --- Freshness: directly encodes the hardware-observed lesson that a
// stale "last known" state must never be treated as current. ---

void test_freshly_observed_neighbor_is_fresh() {
  NeighborTable table;
  table.onFrameObserved(1, 1000);
  TEST_ASSERT_TRUE(table.isFresh(1, 1000));
  TEST_ASSERT_TRUE(table.isFresh(1, 1000 + NeighborTable::kDefaultStaleAfterMs));
}

void test_neighbor_older_than_stale_threshold_is_not_fresh() {
  NeighborTable table;
  table.onFrameObserved(1, 1000);
  TEST_ASSERT_FALSE(
      table.isFresh(1, 1000 + NeighborTable::kDefaultStaleAfterMs + 1));
}

void test_unknown_neighbor_is_never_fresh() {
  NeighborTable table;
  TEST_ASSERT_FALSE(table.isFresh(99, 1000));
}

void test_custom_stale_threshold_is_respected() {
  NeighborTable table;
  table.onFrameObserved(1, 1000);
  TEST_ASSERT_TRUE(table.isFresh(1, 1500, /*staleAfterMs=*/1000));
  TEST_ASSERT_FALSE(table.isFresh(1, 2500, /*staleAfterMs=*/1000));
}

// --- Fixed capacity, no dynamic allocation ---

void test_table_full_drops_new_neighbor_without_evicting_existing() {
  NeighborTable table;
  for (uint8_t i = 0; i < NeighborTable::kMaxNeighbors; ++i) {
    table.onFrameObserved(i, 1000);
  }
  TEST_ASSERT_EQUAL_UINT32(NeighborTable::kMaxNeighbors, table.count());

  // One more, brand-new address - table is full.
  table.onFrameObserved(200, 2000);

  TEST_ASSERT_EQUAL_UINT32(NeighborTable::kMaxNeighbors, table.count());
  TEST_ASSERT_FALSE(table.get(200).known);
  TEST_ASSERT_EQUAL_UINT32(1, table.droppedNewNeighborCount());

  // Existing neighbor 0 must be untouched.
  TEST_ASSERT_TRUE(table.get(0).known);
}

void test_table_full_still_updates_existing_neighbors() {
  NeighborTable table;
  for (uint8_t i = 0; i < NeighborTable::kMaxNeighbors; ++i) {
    table.onFrameObserved(i, 1000);
  }

  // Updating an existing neighbor must still work even when full.
  table.onFrameObserved(0, 5000);
  NeighborInfo info = table.get(0);
  TEST_ASSERT_EQUAL_UINT32(5000, info.lastSeenMs);
  TEST_ASSERT_EQUAL_UINT32(2, info.rxFrames);
  TEST_ASSERT_EQUAL_UINT32(0, table.droppedNewNeighborCount());
}

// --- Reset ---

void test_reset_clears_all_neighbors_and_counters() {
  NeighborTable table;
  table.onFrameObserved(1, 1000);
  table.onFrameObserved(2, 1000);
  table.reset();

  TEST_ASSERT_EQUAL_UINT32(0, table.count());
  TEST_ASSERT_FALSE(table.get(1).known);
  TEST_ASSERT_FALSE(table.get(2).known);
  TEST_ASSERT_EQUAL_UINT32(0, table.droppedNewNeighborCount());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_unknown_address_returns_default_info);
  RUN_TEST(test_first_observation_creates_entry);
  RUN_TEST(test_repeated_observation_updates_existing_entry_not_duplicate);
  RUN_TEST(test_gap_count_accumulates_across_observations);
  RUN_TEST(test_multiple_distinct_neighbors_tracked_independently);
  RUN_TEST(test_freshly_observed_neighbor_is_fresh);
  RUN_TEST(test_neighbor_older_than_stale_threshold_is_not_fresh);
  RUN_TEST(test_unknown_neighbor_is_never_fresh);
  RUN_TEST(test_custom_stale_threshold_is_respected);
  RUN_TEST(test_table_full_drops_new_neighbor_without_evicting_existing);
  RUN_TEST(test_table_full_still_updates_existing_neighbors);
  RUN_TEST(test_reset_clears_all_neighbors_and_counters);
  return UNITY_END();
}
