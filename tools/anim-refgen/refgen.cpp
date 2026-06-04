#include <string>
// Reference-vector generator for the AnimationPlayer Kotlin port fidelity test.
//
// Copies the EXACT integer expressions from lib/Lightnet/Panel/AnimationPlayer.cpp and
// lib/Lightnet/Common/Palette.hpp, then prints currentColor for representative inputs.
// The Kotlin test (PanelAnimationPlayerTest) asserts its port reproduces these numbers,
// validating that Kotlin's arithmetic (ushr, Int division, 0xFF masking) matches C++ uint8/uint16.
//
// Build & run:  g++ -O2 -std=c++17 refgen.cpp -o refgen && ./refgen

#include <cstdint>
#include <cstdio>

struct RGB {
    uint8_t r, g, b;
};
struct GradientStop {
    uint8_t pos, r, g, b;
};

// ── verbatim from AnimationPlayer.cpp ────────────────────────────────────────
static uint8_t lerp8(uint8_t a, uint8_t b, uint8_t frac_q8)
{
    if (a == b || frac_q8 == 0) return a;

    if (frac_q8 == 255) return b;

    if (a < b) return a + (uint8_t)(((uint32_t)(b - a) * frac_q8) >> 8);
    else return a - (uint8_t)(((uint32_t)(a - b) * frac_q8) >> 8);
}

static RGB rgbLerp(RGB a, RGB b, uint8_t f)
{
    return RGB{ lerp8(a.r, b.r, f), lerp8(a.g, b.g, f), lerp8(a.b, b.b, f) };
}

// ── verbatim from Palette.hpp ────────────────────────────────────────────────
static void samplePalette(
    const GradientStop *stops,
    uint8_t             count,
    uint8_t             pos,
    uint8_t *           out_r,
    uint8_t *           out_g,
    uint8_t *           out_b
)
{
    if (count == 0) {
        *out_r = *out_g = *out_b = 0xFF;

        return;
    }

    if (count == 1 || pos <= stops[0].pos) {
        *out_r = stops[0].r;
        *out_g = stops[0].g;
        *out_b = stops[0].b;

        return;
    }

    if (pos >= stops[count - 1].pos) {
        const GradientStop& s = stops[count - 1];

        *out_r = s.r;
        *out_g = s.g;
        *out_b = s.b;

        return;
    }

    uint8_t i = 0;

    while (i + 1 < count && stops[i + 1].pos <= pos)i++;

    const GradientStop& a = stops[i];
    const GradientStop& b = stops[i + 1];
    uint8_t span = (uint8_t)(b.pos - a.pos);

    if (span == 0) {
        *out_r = a.r;
        *out_g = a.g;
        *out_b = a.b;

        return;
    }

    uint8_t frac_q8 = (uint8_t)(((uint16_t)(pos - a.pos) * 255) / span);

    *out_r = (uint8_t)(a.r + (((int16_t)b.r - (int16_t)a.r) * frac_q8) / 255);
    *out_g = (uint8_t)(a.g + (((int16_t)b.g - (int16_t)a.g) * frac_q8) / 255);
    *out_b = (uint8_t)(a.b + (((int16_t)b.b - (int16_t)a.b) * frac_q8) / 255);
}

// ── tick math (verbatim expressions, from/to passed as resolved RGB) ─────────
static RGB fade(uint16_t durationMs, RGB from, RGB to, uint16_t elapsed)
{
    uint8_t progress_q8;

    if (durationMs > 0) {
        uint32_t prog = (uint32_t)elapsed * 256 / durationMs;

        progress_q8 = (prog > 255)?255:(uint8_t)prog;
    } else progress_q8 = 255;

    return rgbLerp(from, to, progress_q8);
}

static RGB breathe(uint16_t durationMs, RGB from, RGB to, uint16_t elapsed)
{
    uint16_t t = elapsed % durationMs;
    uint16_t half = durationMs / 2;
    uint8_t phase_q8 = (t <= half)?(uint8_t)((uint32_t)t * 255 / half):(uint8_t)(((uint32_t)(durationMs - t) * 255) / half);
    uint8_t inv = (uint8_t)(255 - phase_q8);
    uint8_t inv_sq = (uint8_t)(((uint16_t)inv * inv) >> 8);
    uint8_t ease = (uint8_t)(255 - inv_sq);

    return rgbLerp(from, to, ease);
}

static RGB pulse(uint16_t durationMs, uint8_t p1, uint8_t p2, RGB from, RGB to, uint16_t elapsed)
{
    uint8_t rise_pct = p1, fall_pct = p2;
    uint16_t sum = (uint16_t)rise_pct + fall_pct;

    if (sum > 255) {
        rise_pct = (uint8_t)(255 * rise_pct / sum);
        fall_pct = (uint8_t)(255 - rise_pct);
    }

    uint8_t hold_pct = 255 - rise_pct - fall_pct;
    uint16_t rise_ms = (uint32_t)durationMs * rise_pct / 256;
    uint16_t hold_ms = (uint32_t)durationMs * hold_pct / 256;
    uint8_t progress_q8;

    if (elapsed < rise_ms)progress_q8 = (uint8_t)((uint32_t)elapsed * 256 / (rise_ms + 1));
    else if (elapsed < rise_ms + hold_ms)progress_q8 = 255;
    else {
        uint16_t fe = elapsed - rise_ms - hold_ms;
        uint16_t fd = durationMs - rise_ms - hold_ms;

        progress_q8 = 255 - (uint8_t)((uint32_t)fe * 256 / (fd + 1));
    }

    return rgbLerp(from, to, progress_q8);
}

static RGB hue(uint8_t speed, uint16_t elapsed)
{
    if (speed == 0)speed = 1;

    uint16_t hue_step = ((uint32_t)elapsed * speed / 10) % 1530;
    uint8_t seg = hue_step / 255, rem = hue_step % 255;
    uint8_t r = 0, g = 0, b = 0;

    switch (seg) {
        case 0: r = 255;
            g = rem;
            b = 0;
            break;
        case 1: r = 255 - rem;
            g = 255;
            b = 0;
            break;
        case 2: r = 0;
            g = 255;
            b = rem;
            break;
        case 3: r = 0;
            g = 255 - rem;
            b = 255;
            break;
        case 4: r = rem;
            g = 0;
            b = 255;
            break;
        case 5: r = 255;
            g = 0;
            b = 255 - rem;
            break;
    }

    return RGB{ r, g, b };
}

static RGB blink(uint8_t period_ms, RGB from, RGB to, uint16_t elapsed)
{
    if (period_ms == 0)period_ms = 1;

    uint16_t phase = elapsed % (period_ms * 2);

    return (phase < period_ms)?to:from;
}

static RGB strobe(uint8_t hz, RGB to, uint16_t elapsed)
{
    if (hz == 0)hz = 1;

    uint16_t period_ms = 1000 / hz;
    bool on = (elapsed % period_ms) < (period_ms / 2);

    return on?to:RGB{ 0, 0, 0 };
}

static void pr(const char *tag, RGB c)
{
    printf("%s %d,%d,%d\n", tag, c.r, c.g, c.b);
}

int main()
{
    printf("# lerp8\n");

    uint8_t fr[] = { 0, 1, 64, 128, 200, 254, 255 };

    for (uint8_t f: fr) printf("lerp8 0 200 %d -> %d\n", f, lerp8(0, 200, f));

    for (uint8_t f: fr) printf("lerp8 200 0 %d -> %d\n", f, lerp8(200, 0, f));

    printf("# samplePalette (stops: 0=#000000, 128=#FF0000, 255=#00FF00)\n");

    GradientStop pal[3] = { { 0, 0, 0, 0 }, { 128, 255, 0, 0 }, { 255, 0, 255, 0 } };
    uint8_t poss[] = { 0, 32, 64, 127, 128, 160, 200, 255 };

    for (uint8_t pos: poss) {
        uint8_t r, g, b;

        samplePalette(pal, 3, pos, &r, &g, &b);
        printf("pal %d -> %d,%d,%d\n", pos, r, g, b);
    }

    RGB black = { 0, 0, 0 }, white = { 255, 255, 255 }, amber = { 200, 100, 50 };

    printf("# fade dur=1000 black->amber\n");

    uint16_t fe[] = { 0, 100, 250, 500, 750, 900, 1000, 2000 };

    for (uint16_t e: fe) pr(("fade " + std::to_string(e)).c_str(), fade(1000, black, amber, e));

    printf("# breathe dur=2000 black->white\n");

    uint16_t be[] = { 0, 250, 500, 1000, 1500, 1750, 1999 };

    for (uint16_t e: be) pr(("breathe " + std::to_string(e)).c_str(), breathe(2000, black, white, e));

    printf("# pulse dur=1000 rise=64 fall=64 black->white\n");

    uint16_t pe[] = { 0, 50, 124, 200, 300, 400, 600, 800, 999 };

    for (uint16_t e: pe) pr(("pulse " + std::to_string(e)).c_str(), pulse(1000, 64, 64, black, white, e));

    printf("# hue speed=50\n");

    uint16_t he[] = { 0, 50, 100, 200, 306, 500, 800, 1000 };

    for (uint16_t e: he) pr(("hue " + std::to_string(e)).c_str(), hue(50, e));

    printf("# blink period=100 black->white\n");

    uint16_t ble[] = { 0, 50, 99, 100, 150, 199, 200 };

    for (uint16_t e: ble) pr(("blink " + std::to_string(e)).c_str(), blink(100, black, white, e));

    printf("# strobe hz=10 ->red\n");

    RGB red = { 255, 0, 0 };
    uint16_t se[] = { 0, 25, 49, 50, 75, 99, 100 };

    for (uint16_t e: se) pr(("strobe " + std::to_string(e)).c_str(), strobe(10, red, e));

    return 0;
}
