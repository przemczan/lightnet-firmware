// Native unit tests for lib/Lightnet/Core/Controller/Scene/RunnerMath.hpp
// Run with: pio test -e native -f test_runner_math

#include <unity.h>
#include "Core/Controller/Scene/RunnerMath.hpp"

using namespace Lightnet;

// ---- WAVE -----------------------------------------------------------------

void test_wave_center_sweep()
{
    // width==3 reproduces the legacy -1.5 → maxCoord+1.5 sweep exactly.
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -1.5f, waveCenterAt(0.0f, 5, 3));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 6.5f, waveCenterAt(1.0f, 5, 3));

    // Margin scales with width: sweep is -halfW → maxCoord+halfW so the farthest panel
    // fully fades at t=1 regardless of width.
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -2.5f, waveCenterAt(0.0f, 5, 5));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 7.5f, waveCenterAt(1.0f, 5, 5));

    // At t=1 the farthest panel (coord==maxCoord) sits exactly at the trailing edge → off.
    TEST_ASSERT_EQUAL_UINT8(0, waveBrightness(5.0f, waveCenterAt(1.0f, 5, 5), 5));
}

void test_wave_brightness_triangle()
{
    // center=2, width=2 → half-width 1.
    TEST_ASSERT_EQUAL_UINT8(255, waveBrightness(2.0f, 2.0f, 2)); // at centre
    TEST_ASSERT_EQUAL_UINT8(127, waveBrightness(2.5f, 2.0f, 2)); // halfway out
    TEST_ASSERT_EQUAL_UINT8(0, waveBrightness(3.0f, 2.0f, 2));   // at edge → off
    TEST_ASSERT_EQUAL_UINT8(0, waveBrightness(1.0f, 2.0f, 2));   // other edge → off
}

void test_wave_zero_width_is_off()
{
    TEST_ASSERT_EQUAL_UINT8(0, waveBrightness(2.0f, 2.0f, 0)); // no divide, just off
}

// ---- RIPPLE ---------------------------------------------------------------

void test_ripple_radius_sweep()
{
    // width==2 reproduces the legacy 0 → maxCoord+1 sweep exactly.
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, rippleRadiusAt(0.0f, 4, 2));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.0f, rippleRadiusAt(1.0f, 4, 2));

    // Endpoint extends with width so the ring's trailing edge clears the farthest panel:
    // radius reaches maxCoord + width/2 at t=1.
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 6.5f, rippleRadiusAt(1.0f, 4, 5));

    // At t=1 the farthest panel (point model, coord==far==maxCoord) has fully faded.
    TEST_ASSERT_EQUAL_UINT8(0, rippleBandBrightness(4.0f, 4.0f, rippleRadiusAt(1.0f, 4, 5), 5));
}

void test_ripple_brightness_ring()
{
    // radius=3, width=2 → ring half-width 1.
    TEST_ASSERT_EQUAL_UINT8(255, rippleBrightness(3.0f, 3.0f, 2)); // on the ring
    TEST_ASSERT_EQUAL_UINT8(127, rippleBrightness(3.5f, 3.0f, 2)); // half off
    TEST_ASSERT_EQUAL_UINT8(0, rippleBrightness(2.0f, 3.0f, 2));   // outside ring
}

void test_ripple_zero_width_is_off()
{
    TEST_ASSERT_EQUAL_UINT8(0, rippleBrightness(3.0f, 3.0f, 0));
}

void test_ripple_band_brightness_extent()
{
    // Panel spanning the band [2,5], width=2 → soft edge half-width 1.
    TEST_ASSERT_EQUAL_UINT8(255, rippleBandBrightness(2.0f, 5.0f, 2.0f, 2)); // on near edge
    TEST_ASSERT_EQUAL_UINT8(255, rippleBandBrightness(2.0f, 5.0f, 3.5f, 2)); // inside the band
    TEST_ASSERT_EQUAL_UINT8(255, rippleBandBrightness(2.0f, 5.0f, 5.0f, 2)); // on far edge
    TEST_ASSERT_EQUAL_UINT8(127, rippleBandBrightness(2.0f, 5.0f, 5.5f, 2)); // half past far edge
    TEST_ASSERT_EQUAL_UINT8(127, rippleBandBrightness(2.0f, 5.0f, 1.5f, 2)); // half before near edge
    TEST_ASSERT_EQUAL_UINT8(0, rippleBandBrightness(2.0f, 5.0f, 0.5f, 2));   // outside
}

void test_ripple_band_matches_point_model()
{
    // near == far must reproduce rippleBrightness exactly (the topology/point ripple).
    for (int r = 0; r <= 6; r++) {
        TEST_ASSERT_EQUAL_UINT8(rippleBrightness(3.0f, (float)r, 2),
                                rippleBandBrightness(3.0f, 3.0f, (float)r, 2));
    }

    TEST_ASSERT_EQUAL_UINT8(0, rippleBandBrightness(3.0f, 3.0f, 3.0f, 0)); // zero width off
}

// ---- CHASE ----------------------------------------------------------------

void test_chase_lit_coord_sweep()
{
    TEST_ASSERT_EQUAL_UINT8(0, chaseLitCoord(0.0f, 5));
    TEST_ASSERT_EQUAL_UINT8(3, chaseLitCoord(0.5f, 5));  // (int)(6*0.5)
    TEST_ASSERT_EQUAL_UINT8(5, chaseLitCoord(1.0f, 5));  // clamped to maxCoord
    TEST_ASSERT_EQUAL_UINT8(5, chaseLitCoord(0.99f, 5)); // (int)(5.94)
}

void test_chase_brightness_single_ring()
{
    TEST_ASSERT_EQUAL_UINT8(255, chaseBrightness(3, 3));
    TEST_ASSERT_EQUAL_UINT8(0, chaseBrightness(2, 3));
}

void setUp(void)
{
}

void tearDown(void)
{
}

int main(int /*argc*/, char ** /*argv*/)
{
    UNITY_BEGIN();

    RUN_TEST(test_wave_center_sweep);
    RUN_TEST(test_wave_brightness_triangle);
    RUN_TEST(test_wave_zero_width_is_off);

    RUN_TEST(test_ripple_radius_sweep);
    RUN_TEST(test_ripple_brightness_ring);
    RUN_TEST(test_ripple_zero_width_is_off);
    RUN_TEST(test_ripple_band_brightness_extent);
    RUN_TEST(test_ripple_band_matches_point_model);

    RUN_TEST(test_chase_lit_coord_sweep);
    RUN_TEST(test_chase_brightness_single_ring);

    return UNITY_END();
}
