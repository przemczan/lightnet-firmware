// Native unit tests for lib/Lightnet/Controller/Animations/RunnerSpawn.hpp
// Covers the pure pieces of the RAIN/SPARKLE particle spawner: the PRNG, the spawn-rate
// accumulator, the group-id pool, source->leaf path building, and per-panel drop timing.
// NOTE: these prove the math only — the stateful real-time behaviour (cursor recycling vs.
// draining drops, reaping, broadcast-START reach) is verified on sim/device, not here.
// Run with: pio test -e native -f test_runner_spawn

#include <unity.h>
#include "Controller/Animations/RunnerSpawn.hpp"

using namespace Lightnet;

// ---- PRNG -----------------------------------------------------------------

void test_rand_is_deterministic_for_a_seed()
{
    uint32_t a = 12345, b = 12345;

    TEST_ASSERT_EQUAL_UINT32(spawnRandNext(a), spawnRandNext(b));
    TEST_ASSERT_EQUAL_UINT32(spawnRandNext(a), spawnRandNext(b));
}

void test_rand_recovers_from_zero_state()
{
    uint32_t s = 0;

    TEST_ASSERT_NOT_EQUAL_UINT32(0u, spawnRandNext(s)); // doesn't stick at 0
    TEST_ASSERT_NOT_EQUAL_UINT32(0u, s);
}

void test_rand_below_in_range_and_zero_safe()
{
    uint32_t s = 99;

    for (int i = 0; i < 50; i++) {
        uint32_t v = spawnRandBelow(s, 7);

        TEST_ASSERT_TRUE(v < 7);
    }

    TEST_ASSERT_EQUAL_UINT32(0u, spawnRandBelow(s, 0)); // n==0 is safe
}

// ---- Spawn-rate accumulator -----------------------------------------------

void test_due_count_matches_rate_over_a_second()
{
    // 5 waves/s, fed exactly 1000ms in one go → 5 drops (burst cap high enough).
    uint32_t acc = 0;

    TEST_ASSERT_EQUAL_UINT8(5, spawnDueCount(acc, 1000, 5, 16));
}

void test_due_count_accumulates_across_ticks()
{
    // 10/s = one drop every 100ms. Three 60ms ticks (180ms) → one drop, 80ms carried.
    uint32_t acc = 0;

    TEST_ASSERT_EQUAL_UINT8(0, spawnDueCount(acc, 60, 10, 16));
    TEST_ASSERT_EQUAL_UINT8(1, spawnDueCount(acc, 60, 10, 16));
    TEST_ASSERT_EQUAL_UINT8(0, spawnDueCount(acc, 60, 10, 16));
    TEST_ASSERT_EQUAL_UINT8(1, spawnDueCount(acc, 60, 10, 16)); // 240ms total → 2 drops so far
}

void test_due_count_burst_capped_and_no_backlog_spiral()
{
    // Huge dt with a low cap → cap the burst and discard the backlog (acc <= one interval).
    uint32_t acc = 0;
    uint8_t n   = spawnDueCount(acc, 100000, 10, 4);  // would be 1000 drops uncapped

    TEST_ASSERT_EQUAL_UINT8(4, n);
    TEST_ASSERT_TRUE(acc <= 100); // backlog dropped, no spiral next tick
}

void test_due_count_zero_waves_never_spawns()
{
    uint32_t acc = 0;

    TEST_ASSERT_EQUAL_UINT8(0, spawnDueCount(acc, 5000, 0, 16));
}

// ---- Group-id pool --------------------------------------------------------

void test_pool_round_robins_and_persists_cursor()
{
    uint16_t cursor = 0;

    TEST_ASSERT_EQUAL_UINT8(192, spawnPoolNext(cursor, 192, 4));
    TEST_ASSERT_EQUAL_UINT8(193, spawnPoolNext(cursor, 192, 4));
    TEST_ASSERT_EQUAL_UINT8(194, spawnPoolNext(cursor, 192, 4));
    TEST_ASSERT_EQUAL_UINT8(195, spawnPoolNext(cursor, 192, 4));
    TEST_ASSERT_EQUAL_UINT8(192, spawnPoolNext(cursor, 192, 4)); // wraps
    // Cursor persisting means the NEXT id keeps advancing, never resets to base mid-stream.
    TEST_ASSERT_EQUAL_UINT8(193, spawnPoolNext(cursor, 192, 4));
}

// ---- Source->leaf path ----------------------------------------------------

void test_build_path_root_to_leaf_order()
{
    // Tree: 0(root) -> 1 -> 2 -> 3(leaf). parent[root]=root (self).
    uint8_t parent[4] = { 0, 0, 1, 2 };
    uint8_t out[8];

    uint8_t len = spawnBuildPath(parent, /*leaf=*/ 3, /*root=*/ 0, out, 8);

    TEST_ASSERT_EQUAL_UINT8(4, len);
    TEST_ASSERT_EQUAL_UINT8(0, out[0]); // source first
    TEST_ASSERT_EQUAL_UINT8(1, out[1]);
    TEST_ASSERT_EQUAL_UINT8(2, out[2]);
    TEST_ASSERT_EQUAL_UINT8(3, out[3]); // leaf last
}

void test_build_path_sentinel_parent_supported()
{
    // Same shape but root's parent is the 0xFF sentinel instead of self.
    uint8_t parent[4] = { 0xFF, 0, 1, 2 };
    uint8_t out[8];

    TEST_ASSERT_EQUAL_UINT8(4, spawnBuildPath(parent, 3, 0, out, 8));
    TEST_ASSERT_EQUAL_UINT8(0, out[0]);
}

void test_build_path_leaf_not_under_root_returns_zero()
{
    // 2's branch reaches root 0, but we ask for root 9 → not reachable → 0.
    uint8_t parent[4] = { 0, 0, 1, 2 };
    uint8_t out[8];

    TEST_ASSERT_EQUAL_UINT8(0, spawnBuildPath(parent, 3, 9, out, 8));
}

void test_build_path_too_long_for_buffer_returns_zero()
{
    uint8_t parent[4] = { 0, 0, 1, 2 };
    uint8_t out[2];

    TEST_ASSERT_EQUAL_UINT8(0, spawnBuildPath(parent, 3, 0, out, 2)); // needs 4, buffer is 2
}

// ---- Drop timing ----------------------------------------------------------

void test_sparkle_flash_is_instant_on_then_fade()
{
    DropPulse p = sparkleFlash(600);

    TEST_ASSERT_EQUAL_UINT16(0, p.startDelayMs);
    TEST_ASSERT_EQUAL_UINT16(600, p.durationMs);
    TEST_ASSERT_EQUAL_UINT8(0, p.risePct);   // instant onset
    TEST_ASSERT_EQUAL_UINT8(255, p.fallPct); // fades the whole way
}

void test_rain_drop_head_delay_scales_with_position()
{
    // fall=900ms across a 4-node path (span=3 → ringTime=300). Head reaches pos by pos*300.
    DropPulse src = rainDropAt(900, /*width=*/ 2, /*pos=*/ 0, /*pathLen=*/ 4);
    DropPulse mid = rainDropAt(900, 2, 1, 4);
    DropPulse end = rainDropAt(900, 2, 3, 4);

    TEST_ASSERT_EQUAL_UINT16(0, src.startDelayMs);
    TEST_ASSERT_EQUAL_UINT16(300, mid.startDelayMs);
    TEST_ASSERT_EQUAL_UINT16(900, end.startDelayMs);
    // Tail of `width` rings: 2 * 300 = 600ms fade per panel, sharp head.
    TEST_ASSERT_EQUAL_UINT16(600, mid.durationMs);
    TEST_ASSERT_EQUAL_UINT8(0, mid.risePct);
    TEST_ASSERT_EQUAL_UINT8(255, mid.fallPct);
}

void test_rain_tailless_drop_is_a_one_ring_blip()
{
    DropPulse p = rainDropAt(900, /*width=*/ 0, 1, 4); // ringTime=300

    TEST_ASSERT_EQUAL_UINT16(300, p.durationMs);       // 1 ring, not 0
}

void test_rain_single_panel_path_is_safe()
{
    DropPulse p = rainDropAt(900, 2, 0, 1); // span clamps to 1, no divide-by-zero

    TEST_ASSERT_EQUAL_UINT16(0, p.startDelayMs);
    TEST_ASSERT_TRUE(p.durationMs > 0);
}

void setUp(void)
{
}

void tearDown(void)
{
}

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_rand_is_deterministic_for_a_seed);
    RUN_TEST(test_rand_recovers_from_zero_state);
    RUN_TEST(test_rand_below_in_range_and_zero_safe);

    RUN_TEST(test_due_count_matches_rate_over_a_second);
    RUN_TEST(test_due_count_accumulates_across_ticks);
    RUN_TEST(test_due_count_burst_capped_and_no_backlog_spiral);
    RUN_TEST(test_due_count_zero_waves_never_spawns);

    RUN_TEST(test_pool_round_robins_and_persists_cursor);

    RUN_TEST(test_build_path_root_to_leaf_order);
    RUN_TEST(test_build_path_sentinel_parent_supported);
    RUN_TEST(test_build_path_leaf_not_under_root_returns_zero);
    RUN_TEST(test_build_path_too_long_for_buffer_returns_zero);

    RUN_TEST(test_sparkle_flash_is_instant_on_then_fade);
    RUN_TEST(test_rain_drop_head_delay_scales_with_position);
    RUN_TEST(test_rain_tailless_drop_is_a_one_ring_blip);
    RUN_TEST(test_rain_single_panel_path_is_safe);

    return UNITY_END();
}
