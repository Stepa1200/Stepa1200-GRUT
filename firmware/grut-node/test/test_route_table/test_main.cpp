#include <unity.h>

#include "RouteTable.h"

using grut::routing::RouteEntry;
using grut::routing::RouteTable;

void setUp() {}
void tearDown() {}

// 1. insert first route
void test_insert_first_route() {
  RouteTable table;
  const bool ok = table.upsert(/*destination=*/10, /*nextHop=*/1,
                               /*hopCount=*/1, /*nowMs=*/1000);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<unsigned>(table.count()));
}

// 2. lookup
void test_lookup_returns_correct_fields() {
  RouteTable table;
  table.upsert(10, 1, 2, 1000);

  RouteEntry entry;
  const bool found = table.lookup(10, &entry);
  TEST_ASSERT_TRUE(found);
  TEST_ASSERT_TRUE(entry.valid);
  TEST_ASSERT_EQUAL_UINT8(10, entry.destination);
  TEST_ASSERT_EQUAL_UINT8(1, entry.nextHop);
  TEST_ASSERT_EQUAL_UINT8(2, entry.hopCount);
  TEST_ASSERT_EQUAL_UINT32(1000, entry.lastUpdatedMs);
}

void test_lookup_unknown_destination_fails() {
  RouteTable table;
  RouteEntry entry;
  TEST_ASSERT_FALSE(table.lookup(99, &entry));
}

// 3. refresh same route (case 2: same nextHop)
void test_refresh_same_next_hop_updates_hopcount_and_time() {
  RouteTable table;
  table.upsert(10, 1, 3, 1000);
  const bool ok = table.upsert(10, 1, 2, 2000);  // same nextHop=1, new hopCount
  TEST_ASSERT_TRUE(ok);

  RouteEntry entry;
  table.lookup(10, &entry);
  TEST_ASSERT_EQUAL_UINT8(2, entry.hopCount);
  TEST_ASSERT_EQUAL_UINT32(2000, entry.lastUpdatedMs);
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<unsigned>(table.count()));  // no duplicate
}

// 4. lower-hop route replaces higher-hop route (case 3)
void test_lower_hop_count_replaces_existing_fresh_route() {
  RouteTable table;
  table.upsert(10, 1, 3, 1000);  // via nextHop=1, hop=3
  const bool ok = table.upsert(10, 2, 1, 1500);  // via nextHop=2, hop=1 (better)
  TEST_ASSERT_TRUE(ok);

  RouteEntry entry;
  table.lookup(10, &entry);
  TEST_ASSERT_EQUAL_UINT8(2, entry.nextHop);
  TEST_ASSERT_EQUAL_UINT8(1, entry.hopCount);
}

// 5. higher-hop route does NOT replace better route (case 4, one direction)
void test_higher_hop_count_does_not_replace_existing_fresh_route() {
  RouteTable table;
  table.upsert(10, 1, 1, 1000);  // via nextHop=1, hop=1 (good)
  const bool ok = table.upsert(10, 2, 3, 1500);  // via nextHop=2, hop=3 (worse)
  TEST_ASSERT_FALSE(ok);
  TEST_ASSERT_EQUAL_UINT32(1, table.rejectedUpsertCount());

  RouteEntry entry;
  table.lookup(10, &entry);
  TEST_ASSERT_EQUAL_UINT8(1, entry.nextHop);  // unchanged
  TEST_ASSERT_EQUAL_UINT8(1, entry.hopCount);
}

// 6. equal-hop different-nextHop does not flap (case 4, the named scenario)
void test_equal_hop_count_different_next_hop_does_not_flap() {
  RouteTable table;
  table.upsert(10, 1, 2, 1000);
  const bool ok = table.upsert(10, 2, 2, 1500);  // same hopCount, different nextHop
  TEST_ASSERT_FALSE(ok);

  RouteEntry entry;
  table.lookup(10, &entry);
  TEST_ASSERT_EQUAL_UINT8(1, entry.nextHop);  // original kept, no flap
}

// 7. stale route can be replaced (case 5) regardless of hop count
void test_stale_route_replaced_even_with_worse_hop_count() {
  RouteTable table;
  table.upsert(10, 1, 1, 1000, /*staleAfterMs=*/5000);  // good route, hop=1

  // Advance well past staleness, then offer a WORSE route (hop=5) via a
  // different next-hop - should still replace, since the old route can
  // no longer be trusted.
  const uint32_t nowMs = 1000 + 5000 + 1;
  const bool ok = table.upsert(10, 2, 5, nowMs, /*staleAfterMs=*/5000);
  TEST_ASSERT_TRUE(ok);

  RouteEntry entry;
  table.lookup(10, &entry);
  TEST_ASSERT_EQUAL_UINT8(2, entry.nextHop);
  TEST_ASSERT_EQUAL_UINT8(5, entry.hopCount);
}

// 8. remove
void test_remove_existing_route() {
  RouteTable table;
  table.upsert(10, 1, 1, 1000);
  const bool removed = table.remove(10);
  TEST_ASSERT_TRUE(removed);
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<unsigned>(table.count()));

  RouteEntry entry;
  TEST_ASSERT_FALSE(table.lookup(10, &entry));
}

void test_remove_unknown_destination_returns_false() {
  RouteTable table;
  TEST_ASSERT_FALSE(table.remove(42));
}

void test_remove_compacts_table_for_dense_enumeration() {
  RouteTable table;
  table.upsert(1, 1, 1, 1000);
  table.upsert(2, 1, 1, 1000);
  table.upsert(3, 1, 1, 1000);

  table.remove(2);  // remove the middle entry

  TEST_ASSERT_EQUAL_UINT32(2, static_cast<unsigned>(table.count()));
  // Enumeration must stay dense (no gap) across [0, count()).
  RouteEntry e0 = table.getByIndex(0);
  RouteEntry e1 = table.getByIndex(1);
  TEST_ASSERT_TRUE(e0.valid);
  TEST_ASSERT_TRUE(e1.valid);
  TEST_ASSERT_EQUAL_UINT8(1, e0.destination);
  TEST_ASSERT_EQUAL_UINT8(3, e1.destination);
}

// 9. table-full behavior
void test_table_full_rejects_new_destination_without_evicting() {
  RouteTable table;
  for (uint8_t i = 0; i < RouteTable::kMaxRoutes; ++i) {
    table.upsert(static_cast<uint8_t>(10 + i), 1, 1, 1000);
  }
  TEST_ASSERT_EQUAL_UINT32(static_cast<unsigned>(RouteTable::kMaxRoutes),
                           static_cast<unsigned>(table.count()));

  const bool ok = table.upsert(200, 1, 1, 2000);  // brand new destination
  TEST_ASSERT_FALSE(ok);
  TEST_ASSERT_EQUAL_UINT32(static_cast<unsigned>(RouteTable::kMaxRoutes),
                           static_cast<unsigned>(table.count()));
  TEST_ASSERT_EQUAL_UINT32(1, table.droppedNewRouteCount());

  RouteEntry entry;
  TEST_ASSERT_FALSE(table.lookup(200, &entry));
  // Existing routes untouched.
  TEST_ASSERT_TRUE(table.lookup(10, &entry));
}

// 10. duplicate destination does not consume another slot
void test_duplicate_destination_does_not_consume_extra_slot() {
  RouteTable table;
  table.upsert(10, 1, 1, 1000);
  table.upsert(10, 1, 1, 2000);  // same destination, same nextHop
  table.upsert(10, 2, 1, 3000);  // same destination, different (equal-hop) nextHop - rejected, still one slot
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<unsigned>(table.count()));
}

// 11. millis() rollover-safe freshness
void test_freshness_is_millis_rollover_safe() {
  RouteTable table;
  const uint32_t justBeforeRollover = 0xFFFFFFFFu - 100;  // near UINT32_MAX
  table.upsert(10, 1, 1, justBeforeRollover);

  // nowMs has wrapped around past 0.
  const uint32_t afterRollover = 50;  // 100 + 50 = 150ms elapsed, wrapping correctly
  TEST_ASSERT_TRUE(table.isFresh(10, afterRollover, /*staleAfterMs=*/5000));

  // And confirm upsert() itself computes staleness correctly across
  // the same rollover boundary (case 5 path).
  const uint32_t wellPastRollover = 100 + 5000 + 1;  // now clearly stale
  const bool ok = table.upsert(10, 2, 9, wellPastRollover, /*staleAfterMs=*/5000);
  TEST_ASSERT_TRUE(ok);  // stale -> replacement allowed despite worse hopCount
}

// 12. reset
void test_reset_clears_everything() {
  RouteTable table;
  table.upsert(10, 1, 1, 1000);
  table.upsert(11, 1, 1, 1000);
  table.upsert(10, 2, 5, 1500);  // rejected (equal/worse), bumps rejectedUpsertCount

  table.reset();

  TEST_ASSERT_EQUAL_UINT32(0, static_cast<unsigned>(table.count()));
  TEST_ASSERT_EQUAL_UINT32(0, table.droppedNewRouteCount());
  TEST_ASSERT_EQUAL_UINT32(0, table.rejectedUpsertCount());
  RouteEntry entry;
  TEST_ASSERT_FALSE(table.lookup(10, &entry));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_insert_first_route);
  RUN_TEST(test_lookup_returns_correct_fields);
  RUN_TEST(test_lookup_unknown_destination_fails);
  RUN_TEST(test_refresh_same_next_hop_updates_hopcount_and_time);
  RUN_TEST(test_lower_hop_count_replaces_existing_fresh_route);
  RUN_TEST(test_higher_hop_count_does_not_replace_existing_fresh_route);
  RUN_TEST(test_equal_hop_count_different_next_hop_does_not_flap);
  RUN_TEST(test_stale_route_replaced_even_with_worse_hop_count);
  RUN_TEST(test_remove_existing_route);
  RUN_TEST(test_remove_unknown_destination_returns_false);
  RUN_TEST(test_remove_compacts_table_for_dense_enumeration);
  RUN_TEST(test_table_full_rejects_new_destination_without_evicting);
  RUN_TEST(test_duplicate_destination_does_not_consume_extra_slot);
  RUN_TEST(test_freshness_is_millis_rollover_safe);
  RUN_TEST(test_reset_clears_everything);
  return UNITY_END();
}
