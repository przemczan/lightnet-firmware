// Native unit tests for lib/Lightnet/Core/Controller/RunnerCompile.hpp
// Verifies the per-panel PULSE inversion lines up with the RunnerMath envelopes:
// the compiled onset is where the envelope turns on, the peak is where it is full,
// and the end is where it turns off again.
// Run with: pio test -e native -f test_runner_compile

#include <unity.h>
#include "Core/Controller/RunnerCompile.hpp"
#include "Core/Controller/RunnerMath.hpp"

using namespace Lightnet;

// ---- WAVE -----------------------------------------------------------------

void test_wave_compile_onset_peak_end()
{
    // coord=2, maxCoord=5, width=3, dur=1000 → denom=8.
    CompiledPulse cp = compileWave(2.0f, 5, 3, 1000);

    TEST_ASSERT_TRUE(cp.lit);
    TEST_ASSERT_EQUAL_UINT16(250, cp.startDelayMs); // 1000 * 2/8
    TEST_ASSERT_EQUAL_UINT16(375, cp.durationMs);   // 1000 * 3/8

    // At onset the envelope brightness for this panel is 0 (just entering the band).
    float tOn = (float)cp.startDelayMs / 1000.0f;

    TEST_ASSERT_EQUAL_UINT8(0, waveBrightness(2.0f, waveCenterAt(tOn, 5, 3), 3));

    // At the pulse midpoint the band centre sits on the panel → ~full brightness
    // (integer ms rounding of the midpoint can land 1 unit below 255).
    float tPeak = (float)(cp.startDelayMs + cp.durationMs / 2) / 1000.0f;

    TEST_ASSERT_INT_WITHIN(2, 255, waveBrightness(2.0f, waveCenterAt(tPeak, 5, 3), 3));

    // At the end the band has passed → 0.
    float tEnd = (float)(cp.startDelayMs + cp.durationMs) / 1000.0f;

    TEST_ASSERT_EQUAL_UINT8(0, waveBrightness(2.0f, waveCenterAt(tEnd, 5, 3), 3));
}

void test_wave_first_panel_starts_immediately()
{
    CompiledPulse cp = compileWave(0.0f, 5, 3, 1000);

    TEST_ASSERT_TRUE(cp.lit);
    TEST_ASSERT_EQUAL_UINT16(0, cp.startDelayMs);
}

void test_wave_zero_width_unlit()
{
    CompiledPulse cp = compileWave(2.0f, 5, 0, 1000);

    TEST_ASSERT_FALSE(cp.lit);
}

void test_wave_window_uniform_across_panels()
{
    // The lit window is the same for every panel; only the onset differs.
    CompiledPulse a = compileWave(1.0f, 6, 2, 2000);
    CompiledPulse b = compileWave(4.0f, 6, 2, 2000);

    TEST_ASSERT_EQUAL_UINT16(a.durationMs, b.durationMs);
    TEST_ASSERT_TRUE(b.startDelayMs > a.startDelayMs);
}

// ---- CHASE ----------------------------------------------------------------

void test_chase_compile_lit_coord()
{
    // coord=2, maxCoord=5, dur=1200 → denom=6.
    CompiledPulse cp = compileChase(2, 5, 1200);

    TEST_ASSERT_TRUE(cp.lit);
    TEST_ASSERT_EQUAL_UINT16(400, cp.startDelayMs); // 1200 * 2/6
    TEST_ASSERT_EQUAL_UINT16(200, cp.durationMs);   // 1200 / 6

    // Throughout the pulse window the chase's lit coordinate equals this panel.
    float tMid = (float)(cp.startDelayMs + cp.durationMs / 2) / 1200.0f;

    TEST_ASSERT_EQUAL_UINT8(2, chaseLitCoord(tMid, 5));
}

void test_chase_first_panel_immediate()
{
    CompiledPulse cp = compileChase(0, 5, 1200);

    TEST_ASSERT_TRUE(cp.lit);
    TEST_ASSERT_EQUAL_UINT16(0, cp.startDelayMs);
}

// ---- RIPPLE ---------------------------------------------------------------

void test_ripple_point_model_matches_envelope()
{
    // near==far=2 (point), maxCoord=4, width=2, dur=1000 → ringW=1, denom=5.
    CompiledPulse cp = compileRipple(2.0f, 2.0f, 4, 2, 1000);

    TEST_ASSERT_TRUE(cp.lit);
    TEST_ASSERT_EQUAL_UINT16(200, cp.startDelayMs); // (2-1)/5 * 1000
    TEST_ASSERT_EQUAL_UINT16(400, cp.durationMs);   // ((2+1)-(2-1))/5 * 1000

    float tOn = (float)cp.startDelayMs / 1000.0f;

    TEST_ASSERT_EQUAL_UINT8(0, rippleBandBrightness(2.0f, 2.0f, rippleRadiusAt(tOn, 4, 2), 2));

    // Full brightness when the ring reaches the panel's near edge.
    float tFull = 2.0f / 5.0f;

    TEST_ASSERT_EQUAL_UINT8(255, rippleBandBrightness(2.0f, 2.0f, rippleRadiusAt(tFull, 4, 2), 2));

    float tEnd = (float)(cp.startDelayMs + cp.durationMs) / 1000.0f;

    TEST_ASSERT_EQUAL_UINT8(0, rippleBandBrightness(2.0f, 2.0f, rippleRadiusAt(tEnd, 4, 2), 2));
}

void test_ripple_band_has_hold_phase()
{
    // A wide band (near=1, far=3) should produce a hold (rise + fall < 255).
    CompiledPulse cp = compileRipple(1.0f, 3.0f, 4, 2, 1000);

    TEST_ASSERT_TRUE(cp.lit);
    TEST_ASSERT_TRUE((uint16_t)cp.risePct + cp.fallPct < 255);
}

void test_ripple_zero_width_unlit()
{
    CompiledPulse cp = compileRipple(2.0f, 2.0f, 4, 0, 1000);

    TEST_ASSERT_FALSE(cp.lit);
}

// ---- Repeating sweeps (`repeat` / WHEEL engine) ----------------------------

void test_repeating_basic_phase_and_width()
{
    // phase=0.25 of a 1000ms period → onset at 250ms; halfWidth=0.1 → edges ~26/255.
    CompiledPulse cp = compileRepeating(0.25f, 0.1f, 1000);

    TEST_ASSERT_TRUE(cp.lit);
    TEST_ASSERT_EQUAL_UINT16(250, cp.startDelayMs);
    TEST_ASSERT_EQUAL_UINT16(1000, cp.durationMs);
    TEST_ASSERT_EQUAL_UINT8(26, cp.risePct);
    TEST_ASSERT_EQUAL_UINT8(26, cp.fallPct);
}

void test_repeating_zero_period_unlit()
{
    CompiledPulse cp = compileRepeating(0.5f, 0.1f, 0);

    TEST_ASSERT_FALSE(cp.lit);
}

// NB: WAVE/RIPPLE/CHASE no longer have a `repeat`/repeatCount-multiplied repeating
// compile — they are always spawner-driven (ScenePlayer::serviceSweepSpawner), firing
// the one-shot compileWave/compileChase/compileRipple above repeatedly on a schedule
// derived from `count`. The spawn-time formula is unit-tested in test_runner_spawn.

// ---- WHEEL ------------------------------------------------------------------

void test_wheel_phase_lands_on_blade_peak()
{
    // 4 blades over a 4000ms rotation → period = 1000ms/blade. turns=0.25 sits
    // exactly on a blade boundary (slot=1.0 → frac=0 → this blade's peak/seam).
    CompiledPulse cp = compileWheel(0.25f, 4, 18, 4000);

    TEST_ASSERT_TRUE(cp.lit);
    TEST_ASSERT_EQUAL_UINT16(0, cp.startDelayMs);
    TEST_ASSERT_EQUAL_UINT16(1000, cp.durationMs);  // rotationMs / lines
    TEST_ASSERT_EQUAL_UINT8(26, cp.risePct);        // thicknessDeg*lines/720 = 0.1 → 26/255
    TEST_ASSERT_EQUAL_UINT8(26, cp.fallPct);
}

void test_wheel_phase_mid_blade_slot()
{
    // turns=0.6 → slot=2.4 → frac=0.4 → onset at 40% into the 1000ms blade period.
    CompiledPulse cp = compileWheel(0.6f, 4, 18, 4000);

    TEST_ASSERT_TRUE(cp.lit);
    TEST_ASSERT_EQUAL_UINT16(400, cp.startDelayMs);
}

void test_wheel_zero_lines_unlit()
{
    CompiledPulse cp = compileWheel(0.5f, 0, 18, 4000);

    TEST_ASSERT_FALSE(cp.lit);
}

void test_wheel_zero_rotation_unlit()
{
    CompiledPulse cp = compileWheel(0.5f, 4, 18, 0);

    TEST_ASSERT_FALSE(cp.lit);
}

void test_wheel_period_floors_at_one_ms()
{
    // rotationMs/lines truncates to 0 for a short rotation with many blades —
    // clamped to 1ms so the panel still gets a valid (if vanishingly brief) loop.
    CompiledPulse cp = compileWheel(0.0f, 6, 18, 3);

    TEST_ASSERT_TRUE(cp.lit);
    TEST_ASSERT_EQUAL_UINT16(1, cp.durationMs);
}

void test_wheel_high_phase_no_clamp()
{
    // 1 blade over 1000ms rotation. turns=0.85 -> slot=0.85 -> phase=0.85.
    // Previously this was clamped to 0.80 -> 800ms.
    CompiledPulse cp = compileWheel(0.85f, 1, 18, 1000);

    TEST_ASSERT_EQUAL_UINT16(850, cp.startDelayMs);
}

// ---- Repeating asymmetric (RAIN/SPARKLE engine) ----------------------------

void test_repeating_asym_independent_rise_fall()
{
    CompiledPulse cp = compileRepeatingAsym(0.25f, 0.1f, 0.3f, 1000);

    TEST_ASSERT_TRUE(cp.lit);
    TEST_ASSERT_EQUAL_UINT16(250, cp.startDelayMs);
    TEST_ASSERT_EQUAL_UINT16(1000, cp.durationMs);
    TEST_ASSERT_EQUAL_UINT8(26, cp.risePct); // 0.1 * 255
    TEST_ASSERT_EQUAL_UINT8(77, cp.fallPct); // 0.3 * 255
}

void test_repeating_asym_zero_period_unlit()
{
    CompiledPulse cp = compileRepeatingAsym(0.5f, 0.1f, 0.3f, 0);

    TEST_ASSERT_FALSE(cp.lit);
}

// NB: RAIN and SPARKLE are no longer compiled — they are particle spawners (RunnerSpawn.hpp /
// ScenePlayer::serviceSpawner), unit-tested in test_runner_spawn. Their old compile tests
// (and the snapPeriodToWindow speed-decoupling tests) were removed with that retirement.

// ---- BOUNCE -------------------------------------------------------------------

void test_bounce_mid_panel_is_symmetric_triangle()
{
    // coord=3, maxCoord=6 (span 6), width=2 (halfW 1): peak at t=3/6=0.5, lit on
    // t ∈ [2/6, 4/6] — a centred, symmetric triangle. rise == fall == 50%.
    CompiledPulse cp = compileBounce(3.0f, 6, 2, 1200);

    TEST_ASSERT_TRUE(cp.lit);
    TEST_ASSERT_EQUAL_UINT16(400, cp.startDelayMs);   // (3-1)/6 * 1200
    TEST_ASSERT_EQUAL_UINT16(400, cp.durationMs);     // (2/6) * 1200
    TEST_ASSERT_EQUAL_UINT8(127, cp.risePct);         // 0.5
    TEST_ASSERT_EQUAL_UINT8(128, cp.fallPct);         // 0.5
}

void test_bounce_near_edge_peaks_at_start()
{
    // coord=0: the band starts at full on the near edge (peak pinned to t=0) and only
    // recedes — instant rise, all fall. This is the reflection point: the previous (reverse)
    // pass ended here at full, so the seam is continuous.
    CompiledPulse cp = compileBounce(0.0f, 6, 2, 1200);

    TEST_ASSERT_TRUE(cp.lit);
    TEST_ASSERT_EQUAL_UINT16(0, cp.startDelayMs);
    TEST_ASSERT_EQUAL_UINT8(0, cp.risePct);           // peak already at the start
    TEST_ASSERT_EQUAL_UINT8(255, cp.fallPct);         // recedes over the whole window
}

void test_bounce_far_edge_peaks_at_end()
{
    // coord==maxCoord: rises to full exactly at the far edge (t=1), then the pass ends and
    // the next (reversed) pass continues from there — the other reflection point.
    CompiledPulse cp = compileBounce(6.0f, 6, 2, 1200);

    TEST_ASSERT_TRUE(cp.lit);
    TEST_ASSERT_EQUAL_UINT8(255, cp.risePct);         // rises across the whole window
    TEST_ASSERT_EQUAL_UINT8(0, cp.fallPct);           // peak pinned to the far edge
}

void test_bounce_zero_width_or_duration_unlit()
{
    TEST_ASSERT_FALSE(compileBounce(3.0f, 6, 0, 1200).lit);
    TEST_ASSERT_FALSE(compileBounce(3.0f, 6, 2, 0).lit);
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

    RUN_TEST(test_wave_compile_onset_peak_end);
    RUN_TEST(test_wave_first_panel_starts_immediately);
    RUN_TEST(test_wave_zero_width_unlit);
    RUN_TEST(test_wave_window_uniform_across_panels);

    RUN_TEST(test_chase_compile_lit_coord);
    RUN_TEST(test_chase_first_panel_immediate);

    RUN_TEST(test_ripple_point_model_matches_envelope);
    RUN_TEST(test_ripple_band_has_hold_phase);
    RUN_TEST(test_ripple_zero_width_unlit);

    RUN_TEST(test_repeating_basic_phase_and_width);
    RUN_TEST(test_repeating_zero_period_unlit);

    RUN_TEST(test_wheel_phase_lands_on_blade_peak);
    RUN_TEST(test_wheel_phase_mid_blade_slot);
    RUN_TEST(test_wheel_zero_lines_unlit);
    RUN_TEST(test_wheel_zero_rotation_unlit);
    RUN_TEST(test_wheel_period_floors_at_one_ms);
    RUN_TEST(test_wheel_high_phase_no_clamp);

    RUN_TEST(test_repeating_asym_independent_rise_fall);
    RUN_TEST(test_repeating_asym_zero_period_unlit);

    RUN_TEST(test_bounce_mid_panel_is_symmetric_triangle);
    RUN_TEST(test_bounce_near_edge_peaks_at_start);
    RUN_TEST(test_bounce_far_edge_peaks_at_end);
    RUN_TEST(test_bounce_zero_width_or_duration_unlit);

    return UNITY_END();
}
