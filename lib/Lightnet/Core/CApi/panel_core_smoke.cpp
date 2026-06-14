// panel_core_smoke.cpp — host smoke test for the C ABI.
//
// Drives the same FADE-midpoint scenario as the native unit test (test/test_panel_anim): a black->
// white fade, duration 1000 ms, sampled at t=500 ms, must read (127,127,127). This proves the C
// ABI round-trips a raw wire packet through the player and reproduces the firmware integer math.

#include "panel_core_c.h"
#include "ProtocolTypes.hpp"   // construct a wire packet (host-side test only)
#include <stdint.h>
#include <stdio.h>

int main()
{
    anim_handle h = anim_create();

    // FADE (ANIM_FADE = 1), group 1, 1000 ms, colorFrom black -> colorTo white.
    ::Protocol::PacketAnimationPrepare pkt = {};

    pkt.animType    = 1;
    pkt.group_id    = 1;
    pkt.durationMs  = 1000;
    pkt.colorFrom   = Lightnet::ColorRef_rgb(0, 0, 0);
    pkt.colorTo     = Lightnet::ColorRef_rgb(255, 255, 255);
    pkt.composeMode = 0;  // COMPOSE_OPAQUE

    anim_prepare(h, reinterpret_cast<const uint8_t *>(&pkt), (int)sizeof(pkt));
    anim_start(h, /*seq*/ 1, /*group*/ 1, /*now*/ 0);
    anim_tick(h, /*now*/ 500);  // 500/1000 -> q8 128 -> lerp(0,255,128) = 127

    uint8_t r = 0, g = 0, b = 0;

    anim_get_color(h, &r, &g, &b);

    int animating = anim_is_animating(h);

    anim_destroy(h);

    if (r != 127 || g != 127 || b != 127 || !animating) {
        printf("FAIL: color=(%u,%u,%u) animating=%d (expected 127,127,127 animating=1)\n",
               r, g, b, animating);

        return 1;
    }

    printf("OK: FADE midpoint = (%u,%u,%u), animating=%d\n", r, g, b, animating);

    return 0;
}
