#ifdef LIGHTNET_TARGET_CONTROLLER

#include "main.h"
#include "demo.hpp"

#if DEMO_ENABLED

extern PanelsController *panelsController;
extern Lightnet::AnimationScheduler *animScheduler;

uint8_t DEMO_PANELS = 0;
uint8_t demoPanelAddrs[30];

// ============================================================================
// Helpers
// ============================================================================

static void setAll(Protocol::ColorRGB rgb, uint8_t brightness)
{
    Protocol::Color c;
    c.rgb = rgb;
    for (uint8_t i = 0; i < DEMO_PANELS; i++) {
        panelsController->setColorAndBrightness(demoPanelAddrs[i], c, brightness);
        panelsController->turnOn(demoPanelAddrs[i]);
    }
}

static void turnOffAll()
{
    for (uint8_t i = 0; i < DEMO_PANELS; i++) {
        panelsController->turnOff(demoPanelAddrs[i]);
    }
    delay(1000);  // ensure all panels have time to turn off before next demo
}

// ============================================================================
// Fix-verification demos
// ============================================================================

// TESTS: tickBreathe lerp8 fix (brightnessTo < brightnessFrom)
// Before the fix, the expression (brightnessTo - brightnessFrom) promoted to
// signed int (-200), then (uint32_t)(-200) ≈ 4.3B — garbage brightness output.
// Visually: should pulse from bright ice-blue down to nearly off and back.
// Wrong output would be random flickering or wrong brightness range.
void demoInvertedBreathe()
{
    PRINTLN("[DEMO] Inverted Breathe");
    Protocol::ColorRGB ice = {60, 160, 255};
    setAll(ice, 220);
    animScheduler->playOnPanels(1, Lightnet::ANIM_BREATHE, Lightnet::FLAG_LOOP, 2000,
        ice, ice, 220, 15, 0, 0,
        demoPanelAddrs, DEMO_PANELS);
    delay(6200);  // ~3 full cycles
    turnOffAll();
}

// TESTS: PREPARE→START race fix (idle-path advanceQueue removed from tick())
// 10 back-to-back 280ms TRANSITION calls. Each cycle: PREPARE arrives while
// panel is idle, tick() may fire in the 300 µs gap before START arrives.
// Old code: tick() drained the queue in that window → panel silently skipped
// the cycle. New code: idle tick() returns early, queue is preserved for START.
// Visually: all 10 color transitions should complete on BOTH panels, no stutters.
void demoRapidTransitions()
{
    PRINTLN("[DEMO] Rapid Transitions");
    static const Protocol::ColorRGB palette[] = {
        {255, 20,  0},    // red
        {0,   200, 50},   // green
        {0,   80,  255},  // blue
        {255, 160, 0},    // amber
        {200, 0,   255},  // violet
    };
    const uint8_t N = sizeof(palette) / sizeof(palette[0]);

    setAll(palette[0], 200);

    for (uint8_t step = 0; step < 10; step++) {
        Protocol::ColorRGB from = palette[step % N];
        Protocol::ColorRGB to   = palette[(step + 1) % N];
        animScheduler->playOnPanels(10 + step, Lightnet::ANIM_TRANSITION, 0, 280,
            from, to, 200, 200, 0, 0,
            demoPanelAddrs, DEMO_PANELS);
        delay(300);  // 20 ms slack after the 280ms animation — race window still hit
    }
    turnOffAll();
}

// TESTS: tickPulse hold_pct clamp fix (rise_pct + fall_pct > 255)
// param1=170 (rise), param2=110 (fall) → sum 280 > 255.
// Before fix: hold_pct = (uint8_t)(255-170-110) = (uint8_t)(-25) = 231 →
//   rise_ms≈332ms, hold_ms≈451ms — animation exits before ever reaching fall
//   phase, looks like a slow fade-in-and-hold rather than a sharp pulse.
// After fix: proportionally clamped, hold_pct=0, sharp rise+fall only.
// Visually: should look like a crisp spike — fast rise then immediate fall.
void demoExtremePulse()
{
    PRINTLN("[DEMO] Extreme Pulse");
    Protocol::ColorRGB magenta = {255, 0, 150};
    Protocol::ColorRGB black   = {0,   0, 0};
    for (uint8_t i = 0; i < DEMO_PANELS; i++) {
        panelsController->turnOn(demoPanelAddrs[i]);
    }
    for (uint8_t rep = 0; rep < 7; rep++) {
        animScheduler->playOnPanels(20 + rep, Lightnet::ANIM_PULSE, 0, 500,
            black, magenta, 0, 255, 170, 110,
            demoPanelAddrs, DEMO_PANELS);
        delay(520);
    }
    turnOffAll();
}

// TESTS: static→member lastBrightness fix in controller-computed runners.
// Runs 3 WaveRunners then 3 RippleRunners. Each runner is a separate heap
// instance. Before the fix, lastBrightness[] was a static local — shared
// across all instances. A second runner would inherit stale brightness from
// the first and suppress I2C writes on its opening frames (panels stuck at
// wrong level at the start of each new runner).
// Visually: each pass should begin cleanly from zero brightness on all panels.
void demoWaveRippleSequence()
{
    PRINTLN("[DEMO] Wave + Ripple sequence");
    Protocol::ColorRGB warm = {255, 180, 60};
    Protocol::ColorRGB cool = {0,   140, 255};
    Protocol::Color c;

    c.rgb = warm;
    for (uint8_t i = 0; i < DEMO_PANELS; i++) {
        panelsController->setColorAndBrightness(demoPanelAddrs[i], c, 0);
        panelsController->turnOn(demoPanelAddrs[i]);
    }

    for (uint8_t p = 0; p < 3; p++) {
        Lightnet::WaveRunner wave(30 + p, demoPanelAddrs, DEMO_PANELS, 1400, 2, warm);
        while (!wave.isFinished()) { wave.tick(millis()); delay(8); }
        delay(150);
    }

    c.rgb = cool;
    for (uint8_t i = 0; i < DEMO_PANELS; i++) {
        panelsController->setColorAndBrightness(demoPanelAddrs[i], c, 0);
    }

    for (uint8_t p = 0; p < 3; p++) {
        uint8_t origin = p % DEMO_PANELS;
        Lightnet::RippleRunner ripple(33 + p, demoPanelAddrs, DEMO_PANELS,
                                      origin, 1400, 2, cool);
        while (!ripple.isFinished()) { ripple.tick(millis()); delay(8); }
        delay(150);
    }

    turnOffAll();
}

// TESTS: tickReactive rgbLerp fix (colorFrom.r/g > colorTo.r/g).
// colorFrom={180,60,10} (warm ember), colorTo={10,10,255} (cold blue).
// R and G channels are inverted (From > To). Before the fix, direct arithmetic
// promoted (colorTo.r - colorFrom.r) to signed int (negative), cast to uint32_t
// ≈ 4.3B, producing garbage color output on every beat.
// After fix: lerp8/rgbLerp handle the direction correctly.
// Visually: each triggered beat should flash from warm ember → cold blue,
// then decay back. Wrong output would produce random colors or max-brightness flicker.
void demoReactiveBeats()
{
    PRINTLN("[DEMO] Reactive Beats");
    Protocol::ColorRGB ember = {180, 60,  10};  // warm — higher R,G than peak
    Protocol::ColorRGB flash = {10,  10,  255}; // cold blue — R,G lower than ember
    setAll(ember, 25);

    // decayRate param1=210: decays from 255 to 0 in ~1.2s between beats
    animScheduler->playOnPanels(40, Lightnet::ANIM_REACTIVE, 0, 0,
        ember, flash, 25, 255, 210, 0,
        demoPanelAddrs, DEMO_PANELS);

    delay(500);  // let animation arm before first beat

    for (uint8_t beat = 0; beat < 8; beat++) {
        animScheduler->triggerGroup(40, 255);
        delay(500);  // ~120 BPM
    }

    delay(1200);  // tail-off: let last trigger decay to ember level
    turnOffAll();
}

// ============================================================================
// Bonus: visually rich effects to close the loop
// ============================================================================

// Panels stagger through a rainbow then fade out together.
void demoRainbowFade()
{
    PRINTLN("[DEMO] Rainbow Fade");
    Protocol::ColorRGB black = {0, 0, 0};
    for (uint8_t i = 0; i < DEMO_PANELS; i++) {
        panelsController->turnOn(demoPanelAddrs[i]);
    }
    animScheduler->playOnPanels(50, Lightnet::ANIM_HUE_CYCLE, 0, 5000,
        black, black, 0, 210, 10, 0,
        demoPanelAddrs, DEMO_PANELS);
    delay(5000);

    uint8_t fadeFlags = Lightnet::FLAG_CURRENT_COLOR_FROM | Lightnet::FLAG_CURRENT_COLOR_TO
                      | Lightnet::FLAG_CURRENT_BRIGHTNESS_FROM;
    animScheduler->playOnPanels(51, Lightnet::ANIM_FADE, fadeFlags, 800,
        black, black, 0, 0, 0, 0,
        demoPanelAddrs, DEMO_PANELS);
    delay(900);
}

// Warm-white brightness wave sweeps across panels, 3 passes, then a chase.
void demoWaveAndChase()
{
    PRINTLN("[DEMO] Wave + Chase");
    Protocol::ColorRGB warm  = {255, 210, 140};
    Protocol::ColorRGB cyan  = {0,   200, 255};
    Protocol::Color c;

    c.rgb = warm;
    for (uint8_t i = 0; i < DEMO_PANELS; i++) {
        panelsController->setColorAndBrightness(demoPanelAddrs[i], c, 0);
        panelsController->turnOn(demoPanelAddrs[i]);
    }
    for (uint8_t p = 0; p < 3; p++) {
        Lightnet::WaveRunner wave(60 + p, demoPanelAddrs, DEMO_PANELS, 1200, 2, warm);
        while (!wave.isFinished()) { wave.tick(millis()); delay(8); }
        delay(120);
    }

    c.rgb = cyan;
    for (uint8_t i = 0; i < DEMO_PANELS; i++) {
        panelsController->setColorAndBrightness(demoPanelAddrs[i], c, 0);
    }
    for (uint8_t p = 0; p < 4; p++) {
        Lightnet::ChaseRunner chase(63 + p, demoPanelAddrs, DEMO_PANELS, 1000, cyan);
        while (!chase.isFinished()) { chase.tick(millis()); delay(8); }
    }
    turnOffAll();
}

// ============================================================================
// Main entry point — called from loop() after network init
// ============================================================================

static uint8_t demoIndex = 0;

void runDemos()
{
    switch (demoIndex) {
        case 0: demoInvertedBreathe();    break;
        case 1: demoRapidTransitions();   break;
        case 2: demoExtremePulse();       break;
        case 3: demoWaveRippleSequence(); break;
        case 4: demoReactiveBeats();      break;
        case 5: demoRainbowFade();        break;
        case 6: demoWaveAndChase();       break;
    }
    demoIndex = (demoIndex + 1) % 7;
}

#endif  // DEMO_ENABLED
#endif  // LIGHTNET_TARGET_CONTROLLER
