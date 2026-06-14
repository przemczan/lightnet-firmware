// Native unit tests for the portable animation core: lib/Lightnet/Core/Panel/AnimationPlayer.
// Run with: pio test -e native -f test_panel_anim
//
// Proves the player compiles and runs on a plain host compiler (no Arduino, no millis(), no
// LED driver) and locks the behaviours that the firmware<->mobile refactor depends on:
//   - time is a parameter (deterministic given `now`)
//   - the player is the single colour authority (currentColor()/takeDirty())
//   - setColorDirect is ungated (the delta-gate regression the white-init mismatch could cause)
//   - FLAG_CURRENT_COLOR_* reads the current output (lastOutput), not a hardware driver
//
// The implementation is compiled in directly (the native env has no lib_extra_dirs for Core).

#include <unity.h>
#include "Core/Panel/AnimationPlayer.cpp"

using namespace Lightnet;

static AnimationPlayer *player;

void setUp(void)
{
    player = new AnimationPlayer();
}

void tearDown(void)
{
    delete player;
}

static ::Protocol::PacketAnimationPrepare makePrepare(
    uint8_t  type,
    uint8_t  group,
    uint16_t durationMs,
    ColorRef from,
    ColorRef to,
    uint8_t  flags = 0
)
{
    ::Protocol::PacketAnimationPrepare p = {};

    p.animType     = type;
    p.group_id     = group;
    p.flags        = flags;
    p.transitionMs = 0;
    p.durationMs   = durationMs;
    p.colorFrom    = from;
    p.colorTo      = to;
    p.param1       = 0;
    p.param2       = 0;
    p.composeMode  = COMPOSE_OPAQUE;
    p.composeOrder = 0;
    p.startDelayMs = 0;

    return p;
}

// ---- setColorDirect: ungated, even to black on a fresh player ------------------------------

void test_setColorDirect_black_on_fresh_player_is_dirty()
{
    // Regression: lastOutput inits to {0,0,0}; a gated path would swallow SET_COLOR(0,0,0)
    // and never report it. setColorDirect must be ungated.
    player->setColorDirect({ 0, 0, 0 });

    TEST_ASSERT_TRUE(player->takeDirty());
    ::Protocol::ColorRGB c = player->currentColor();
    TEST_ASSERT_EQUAL_UINT8(0, c.r);
    TEST_ASSERT_EQUAL_UINT8(0, c.g);
    TEST_ASSERT_EQUAL_UINT8(0, c.b);
    // takeDirty clears the flag
    TEST_ASSERT_FALSE(player->takeDirty());
}

void test_setColorDirect_sets_current_color()
{
    player->setColorDirect({ 10, 20, 30 });

    ::Protocol::ColorRGB c = player->currentColor();
    TEST_ASSERT_EQUAL_UINT8(10, c.r);
    TEST_ASSERT_EQUAL_UINT8(20, c.g);
    TEST_ASSERT_EQUAL_UINT8(30, c.b);
}

// ---- SOLID: holds colorTo -----------------------------------------------------------------

void test_solid_holds_color_to()
{
    auto p = makePrepare(ANIM_SOLID, 1, 0,
                         ColorRef_rgb(0, 0, 0), ColorRef_rgb(100, 150, 200));

    player->prepare(&p);
    player->start(1, 1, 0);
    player->tick(16);  // first tick past the 16ms frame gate

    ::Protocol::ColorRGB c = player->currentColor();
    TEST_ASSERT_EQUAL_UINT8(100, c.r);
    TEST_ASSERT_EQUAL_UINT8(150, c.g);
    TEST_ASSERT_EQUAL_UINT8(200, c.b);
}

// ---- FADE: deterministic given `now` (time is a parameter) ---------------------------------

void test_fade_midpoint_is_time_driven()
{
    auto p = makePrepare(ANIM_FADE, 1, 1000,
                         ColorRef_rgb(0, 0, 0), ColorRef_rgb(255, 255, 255));

    player->prepare(&p);
    player->start(1, 1, 0);        // startMs = 0
    player->tick(500);             // elapsed 500/1000 -> q8 128 -> lerp(0,255,128) = 127

    ::Protocol::ColorRGB c = player->currentColor();
    TEST_ASSERT_EQUAL_UINT8(127, c.r);
    TEST_ASSERT_EQUAL_UINT8(127, c.g);
    TEST_ASSERT_EQUAL_UINT8(127, c.b);
    TEST_ASSERT_TRUE(player->isAnimating());
}

// ---- FLAG_CURRENT_COLOR_FROM: substitutes the current output, not the packet field --------

void test_flag_current_color_from_reads_last_output()
{
    player->setColorDirect({ 200, 50, 10 });  // current output

    // colorFrom in the packet is black; FLAG_CURRENT_COLOR_FROM must replace it with (200,50,10).
    auto p = makePrepare(ANIM_FADE, 2, 1000,
                         ColorRef_rgb(0, 0, 0), ColorRef_rgb(255, 255, 255),
                         FLAG_CURRENT_COLOR_FROM);

    player->prepare(&p);
    player->start(1, 2, 0);
    player->tick(500);  // q8 128: r = 200 + ((255-200)*128>>8) = 200 + 27 = 227

    ::Protocol::ColorRGB c = player->currentColor();
    TEST_ASSERT_EQUAL_UINT8(227, c.r);   // would be 127 if FLAG_CURRENT were ignored
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_setColorDirect_black_on_fresh_player_is_dirty);
    RUN_TEST(test_setColorDirect_sets_current_color);
    RUN_TEST(test_solid_holds_color_to);
    RUN_TEST(test_fade_midpoint_is_time_driven);
    RUN_TEST(test_flag_current_color_from_reads_last_output);

    return UNITY_END();
}
