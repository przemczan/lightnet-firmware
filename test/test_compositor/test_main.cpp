// Native unit tests for lib/Lightnet/Core/Anim/ColorCompose.hpp
// Run with: pio test -e native -f test_compositor

#include <unity.h>
#include "Core/Anim/ColorCompose.hpp"

using namespace Lightnet;

static void assertRGB(RGB8 c, uint8_t r, uint8_t g, uint8_t b)
{
    TEST_ASSERT_EQUAL_UINT8(r, c.r);
    TEST_ASSERT_EQUAL_UINT8(g, c.g);
    TEST_ASSERT_EQUAL_UINT8(b, c.b);
}

// ---- Blend modes ----------------------------------------------------------

void test_compose_opaque_is_top_wins()
{
    RGB8 below = { 10, 20, 30 };
    RGB8 top   = { 200, 100, 50 };

    assertRGB(composeColor(below, top, CO_OPAQUE), 200, 100, 50);
}

void test_compose_add_clamps()
{
    RGB8 a = { 200, 100, 0 };
    RGB8 b = { 100, 100, 5 };

    assertRGB(composeColor(a, b, CO_ADD), 255, 200, 5); // 300→255, 200, 5
}

void test_compose_max()
{
    RGB8 a = { 200, 10, 50 };
    RGB8 b = { 100, 100, 50 };

    assertRGB(composeColor(a, b, CO_MAX), 200, 100, 50);
}

void test_compose_multiply()
{
    RGB8 a = { 255, 128, 0 };
    RGB8 b = { 128, 255, 255 };

    // 255*128/255=128 ; 128*255/255=128 ; 0
    assertRGB(composeColor(a, b, CO_MULTIPLY), 128, 128, 0);
}

void test_compose_multiply_by_white_is_identity()
{
    RGB8 a = { 17, 200, 99 };
    RGB8 white = { 255, 255, 255 };

    assertRGB(composeColor(a, white, CO_MULTIPLY), 17, 200, 99);
}

void test_compose_screen_lightens()
{
    RGB8 a = { 128, 0, 255 };
    RGB8 b = { 128, 0, 0 };

    // screen(128,128)=255-(127*127/255)=255-63=192 ; screen(0,0)=0 ; screen(255,0)=255
    assertRGB(composeColor(a, b, CO_SCREEN), 192, 0, 255);
}

void test_compose_darken()
{
    RGB8 a = { 200, 10, 50 };
    RGB8 b = { 100, 100, 50 };

    assertRGB(composeColor(a, b, CO_DARKEN), 100, 10, 50);
}

void test_compose_overlay()
{
    RGB8 a = { 100, 200, 128 };
    RGB8 b = { 50, 50, 50 };

    // overlay(100,50)=2*100*50/255=39 ; overlay(200,50)=255-2*55*205/255=167 ; overlay(128,50)=255-2*127*205/255=51
    assertRGB(composeColor(a, b, CO_OVERLAY), 39, 167, 51);
}

void test_compose_difference()
{
    RGB8 a = { 200, 10, 50 };
    RGB8 b = { 100, 100, 50 };

    assertRGB(composeColor(a, b, CO_DIFFERENCE), 100, 90, 0);
}

void test_compose_subtract_clamps_to_zero()
{
    RGB8 a = { 200, 10, 50 };
    RGB8 b = { 100, 100, 50 };

    assertRGB(composeColor(a, b, CO_SUBTRACT), 100, 0, 0);
}

// ---- HSV round-trip -------------------------------------------------------

void test_hsv_primaries_roundtrip()
{
    RGB8 colors[] = {
        { 255, 0, 0 }, { 0, 255, 0 }, { 0, 0, 255 },
        { 255, 255, 0 }, { 0, 255, 255 }, { 255, 0, 255 },
        { 255, 255, 255 }, { 0, 0, 0 }, { 128, 128, 128 }
    };

    for (auto c : colors) {
        RGB8 rt = hsv2rgb(rgb2hsv(c));

        // Approximate integer HSV (43-per-sector): the round-trip drifts up to ~9 units
        // on the mixed primaries (e.g. magenta). Accepted cost — modifiers anchor at the
        // identity points (s=255 / h=0), which are exact.
        TEST_ASSERT_INT_WITHIN(12, c.r, rt.r);
        TEST_ASSERT_INT_WITHIN(12, c.g, rt.g);
        TEST_ASSERT_INT_WITHIN(12, c.b, rt.b);
    }
}

void test_grey_has_zero_saturation()
{
    HSV8 h = rgb2hsv({ 90, 90, 90 });

    TEST_ASSERT_EQUAL_UINT8(0, h.s);
    TEST_ASSERT_EQUAL_UINT8(90, h.v);
}

// ---- Modifiers ------------------------------------------------------------

void test_dim_identity_and_scale()
{
    RGB8 c = { 200, 100, 50 };

    assertRGB(modDim(c, 255), 200, 100, 50); // identity
    assertRGB(modDim(c, 0), 0, 0, 0);        // off
    assertRGB(modDim(c, 128), 100, 50, 25);  // ~half
}

void test_desaturate_identity_and_grey()
{
    RGB8 red = { 255, 0, 0 };

    // Identity at 255 (within HSV round-trip tolerance).
    RGB8 same = modDesaturate(red, 255);

    TEST_ASSERT_INT_WITHIN(6, 255, same.r);
    TEST_ASSERT_INT_WITHIN(6, 0, same.g);
    TEST_ASSERT_INT_WITHIN(6, 0, same.b);

    // Zero saturation → grey at the same value (V=255).
    RGB8 grey = modDesaturate(red, 0);

    TEST_ASSERT_EQUAL_UINT8(grey.r, grey.g);
    TEST_ASSERT_EQUAL_UINT8(grey.g, grey.b);
    TEST_ASSERT_EQUAL_UINT8(255, grey.r);
}

void test_brighten_identity_and_boost()
{
    RGB8 c = { 200, 100, 50 };

    assertRGB(modBrighten(c, 0), 200, 100, 50);   // identity
    assertRGB(modBrighten(c, 255), 255, 255, 255); // full boost -> white

    // Halfway: channel + (255-channel)*128/255 ≈ channel + (255-channel)/2
    RGB8 half = modBrighten(c, 128);

    TEST_ASSERT_INT_WITHIN(1, 227, half.r); // 200 + 55*128/255 ≈ 227
    TEST_ASSERT_INT_WITHIN(1, 177, half.g); // 100 + 155*128/255 ≈ 177
    TEST_ASSERT_INT_WITHIN(1, 152, half.b); // 50 + 205*128/255 ≈ 152
}

void test_saturate_identity_and_boost()
{
    // Partially-desaturated red: high V, mid S.
    RGB8 c = modDesaturate({ 255, 0, 0 }, 128); // S ~128

    RGB8 same = modSaturate(c, 0); // identity

    assertRGB(same, c.r, c.g, c.b);

    RGB8 boosted = modSaturate(c, 255); // -> fully saturated

    HSV8 h = rgb2hsv(boosted);

    TEST_ASSERT_INT_WITHIN(2, 255, h.s);
}

void test_saturate_grey_is_identity()
{
    // White (e.g. the output of a full-amount MOD_BRIGHTEN) has no defined hue —
    // rgb2hsv defaults h=0 (red). Boosting saturation must not turn it red.
    assertRGB(modSaturate({ 255, 255, 255 }, 255), 255, 255, 255);
    assertRGB(modSaturate({ 0, 0, 0 }, 255), 0, 0, 0);
    assertRGB(modSaturate({ 100, 100, 100 }, 255), 100, 100, 100);
}

void test_hue_shift_identity_and_rotate()
{
    RGB8 red = { 255, 0, 0 };

    RGB8 same = modHueShift(red, 0); // identity

    TEST_ASSERT_INT_WITHIN(6, 255, same.r);
    TEST_ASSERT_INT_WITHIN(6, 0, same.g);

    // +85 (≈120°) rotates red toward green-dominant.
    RGB8 rot = modHueShift(red, 85);

    TEST_ASSERT_TRUE(rot.g > rot.r);
}

void test_invert_identity_full_and_blend()
{
    RGB8 c = { 200, 100, 0 };

    assertRGB(modInvert(c, 0), 200, 100, 0);     // identity — unchanged
    assertRGB(modInvert(c, 255), 55, 155, 255);  // full invert — 255-r,255-g,255-b
    assertRGB(modInvert(c, 128), 128, 127, 128); // halfway cross-fade toward inverted
}

// ---- Layer fold (the compositor's per-tick contract) ----------------------

static CompositeLayer srcLayer(uint8_t order, RGB8 color, uint8_t op)
{
    CompositeLayer L;

    L.composeOrder = order;
    L.isModifier   = false;
    L.op           = op;
    L.value        = 0;
    L.color        = color;

    return L;
}

static CompositeLayer modLayer(uint8_t order, uint8_t modOp, uint8_t value)
{
    CompositeLayer L;

    L.composeOrder = order;
    L.isModifier   = true;
    L.op           = modOp;
    L.value        = value;
    L.color        = { 0, 0, 0 };

    return L;
}

static const RGB8 BLACK = { 0, 0, 0 };

void test_fold_empty_is_base()
{
    assertRGB(foldLayers(nullptr, 0, BLACK), 0, 0, 0);
    assertRGB(foldLayers(nullptr, 0, RGB8{ 5, 10, 15 }), 5, 10, 15); // idle shows background
}

void test_fold_two_normal_sources_top_wins_by_order()
{
    // Higher composeOrder is on top for an opaque NORMAL source — regardless of array order.
    CompositeLayer ls[] = {
        srcLayer(2, { 10, 20, 30 }, CO_OPAQUE),  // top
        srcLayer(1, { 200, 100, 50 }, CO_OPAQUE) // bottom
    };

    assertRGB(foldLayers(ls, 2, BLACK), 10, 20, 30);
}

void test_fold_add_over_base()
{
    CompositeLayer ls[] = {
        srcLayer(0, { 50, 50, 50 }, CO_OPAQUE), // base
        srcLayer(1, { 60, 0, 250 }, CO_ADD)     // accent, additive
    };

    assertRGB(foldLayers(ls, 2, BLACK), 110, 50, 255); // 50+60, 50+0, 50+250→255
}

void test_fold_max_treats_black_as_transparent()
{
    // A runner accent (black off-phase) over a background must NOT clobber it under MAX.
    CompositeLayer ls[] = {
        srcLayer(0, { 40, 80, 120 }, CO_OPAQUE), // background
        srcLayer(1, { 0, 0, 0 }, CO_MAX)         // accent currently dark
    };

    assertRGB(foldLayers(ls, 2, BLACK), 40, 80, 120); // background shows through
}

void test_fold_runner_over_scene_background()
{
    // The full "wave accent over ambient background" path: base = scene background,
    // a single MAX runner layer whose dark phase leaves the background intact and whose
    // lit phase brightens it.
    CompositeLayer dark[]   = { srcLayer(0, { 0, 0, 0 }, CO_MAX) };
    CompositeLayer litUp[]  = { srcLayer(0, { 200, 60, 0 }, CO_MAX) };
    RGB8 bg = { 0, 0, 40 };

    assertRGB(foldLayers(dark, 1, bg), 0, 0, 40);     // off-phase: background only
    assertRGB(foldLayers(litUp, 1, bg), 200, 60, 40); // lit-phase: max(bg, accent)
}

void test_fold_modifier_dims_layers_below()
{
    // Source then a dim modifier at a higher order halves it.
    CompositeLayer ls[] = {
        srcLayer(0, { 200, 100, 50 }, CO_OPAQUE),
        modLayer(1, MO_DIM, 128)
    };

    assertRGB(foldLayers(ls, 2, BLACK), 100, 50, 25);
}

void test_fold_modifier_boosts_layers_below()
{
    // Source then a brighten modifier at a higher order pushes it toward white.
    CompositeLayer ls[] = {
        srcLayer(0, { 200, 100, 50 }, CO_OPAQUE),
        modLayer(1, MO_BRIGHTEN, 255)
    };

    assertRGB(foldLayers(ls, 2, BLACK), 255, 255, 255);
}

void test_fold_order_matters_for_modifier()
{
    // A modifier only affects what is already accumulated below it. Placing the source
    // ABOVE the modifier (higher order) means the modifier sees only black, and the
    // opaque source then overwrites → unmodified.
    CompositeLayer ls[] = {
        modLayer(0, MO_DIM, 0),                  // dims black → still black
        srcLayer(1, { 200, 100, 50 }, CO_OPAQUE) // opaque on top → wins
    };

    assertRGB(foldLayers(ls, 2, BLACK), 200, 100, 50);
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

    RUN_TEST(test_compose_opaque_is_top_wins);
    RUN_TEST(test_compose_add_clamps);
    RUN_TEST(test_compose_max);
    RUN_TEST(test_compose_multiply);
    RUN_TEST(test_compose_multiply_by_white_is_identity);
    RUN_TEST(test_compose_screen_lightens);
    RUN_TEST(test_compose_darken);
    RUN_TEST(test_compose_overlay);
    RUN_TEST(test_compose_difference);
    RUN_TEST(test_compose_subtract_clamps_to_zero);

    RUN_TEST(test_hsv_primaries_roundtrip);
    RUN_TEST(test_grey_has_zero_saturation);

    RUN_TEST(test_dim_identity_and_scale);
    RUN_TEST(test_desaturate_identity_and_grey);
    RUN_TEST(test_brighten_identity_and_boost);
    RUN_TEST(test_saturate_identity_and_boost);
    RUN_TEST(test_saturate_grey_is_identity);
    RUN_TEST(test_hue_shift_identity_and_rotate);
    RUN_TEST(test_invert_identity_full_and_blend);

    RUN_TEST(test_fold_empty_is_base);
    RUN_TEST(test_fold_two_normal_sources_top_wins_by_order);
    RUN_TEST(test_fold_add_over_base);
    RUN_TEST(test_fold_max_treats_black_as_transparent);
    RUN_TEST(test_fold_runner_over_scene_background);
    RUN_TEST(test_fold_modifier_dims_layers_below);
    RUN_TEST(test_fold_modifier_boosts_layers_below);
    RUN_TEST(test_fold_order_matters_for_modifier);

    return UNITY_END();
}
