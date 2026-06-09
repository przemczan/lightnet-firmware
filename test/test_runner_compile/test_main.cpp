// Native unit tests for lib/Lightnet/Controller/Animations/RunnerCompile.hpp
// Verifies the per-panel PULSE inversion lines up with the RunnerMath envelopes:
// the compiled onset is where the envelope turns on, the peak is where it is full,
// and the end is where it turns off again.
// Run with: pio test -e native -f test_runner_compile

#include <unity.h>
#include "Controller/Animations/RunnerCompile.hpp"
#include "Controller/Animations/RunnerMath.hpp"

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

void test_wave_repeating_matches_band_geometry()
{
    // Same band geometry as compileWave (peak at coord+halfWidth), reframed as a
    // per-cycle phase/halfWidth: coord=2, maxCoord=5, width=3 → denom=8, halfW=1.5.
    CompiledPulse cp = compileWaveRepeating(2.0f, 5, 3, 1000);

    TEST_ASSERT_TRUE(cp.lit);
    TEST_ASSERT_EQUAL_UINT16(438, cp.startDelayMs); // (2+1.5)/8 * 1000
    TEST_ASSERT_EQUAL_UINT16(1000, cp.durationMs);  // == period
    TEST_ASSERT_EQUAL_UINT8(48, cp.risePct);        // (1.5/8) * 255
    TEST_ASSERT_EQUAL_UINT8(48, cp.fallPct);
}

void test_wave_repeating_zero_width_unlit()
{
    CompiledPulse cp = compileWaveRepeating(2.0f, 5, 0, 1000);

    TEST_ASSERT_FALSE(cp.lit);
}

void test_chase_repeating_matches_blip_geometry()
{
    // coord=2, maxCoord=5, period=1200 → denom=6, blip centred on coord+0.5.
    CompiledPulse cp = compileChaseRepeating(2, 5, 1200);

    TEST_ASSERT_TRUE(cp.lit);
    TEST_ASSERT_EQUAL_UINT16(500, cp.startDelayMs); // (2+0.5)/6 * 1200
    TEST_ASSERT_EQUAL_UINT16(1200, cp.durationMs);
    TEST_ASSERT_EQUAL_UINT8(21, cp.risePct);        // (0.5/6) * 255
    TEST_ASSERT_EQUAL_UINT8(21, cp.fallPct);
}

void test_ripple_repeating_point_model_matches_band_geometry()
{
    // near==far=2 (point), maxCoord=4, width=2, period=1000 → ringW=1, denom=5;
    // same centre/half-span the one-shot compileRipple derives from [near,far].
    CompiledPulse cp = compileRippleRepeating(2.0f, 2.0f, 4, 2, 1000);

    TEST_ASSERT_TRUE(cp.lit);
    TEST_ASSERT_EQUAL_UINT16(400, cp.startDelayMs); // 2/5 * 1000
    TEST_ASSERT_EQUAL_UINT16(1000, cp.durationMs);
    TEST_ASSERT_EQUAL_UINT8(51, cp.risePct);        // (1/5) * 255
    TEST_ASSERT_EQUAL_UINT8(51, cp.fallPct);
}

void test_ripple_repeating_zero_width_unlit()
{
    CompiledPulse cp = compileRippleRepeating(2.0f, 2.0f, 4, 0, 1000);

    TEST_ASSERT_FALSE(cp.lit);
}

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
    RUN_TEST(test_wave_repeating_matches_band_geometry);
    RUN_TEST(test_wave_repeating_zero_width_unlit);
    RUN_TEST(test_chase_repeating_matches_blip_geometry);
    RUN_TEST(test_ripple_repeating_point_model_matches_band_geometry);
    RUN_TEST(test_ripple_repeating_zero_width_unlit);

    RUN_TEST(test_wheel_phase_lands_on_blade_peak);
    RUN_TEST(test_wheel_phase_mid_blade_slot);
    RUN_TEST(test_wheel_zero_lines_unlit);
    RUN_TEST(test_wheel_zero_rotation_unlit);
    RUN_TEST(test_wheel_period_floors_at_one_ms);
    RUN_TEST(test_wheel_high_phase_no_clamp);

    return UNITY_END();
}
