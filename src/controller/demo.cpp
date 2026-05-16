#ifdef LIGHTNET_TARGET_CONTROLLER
#ifdef DEMO_ENABLED

#include "main.h"
#include "demo.hpp"

extern PanelsController *panelsController;
extern Lightnet::AnimationScheduler *animScheduler;

uint8_t DEMO_PANELS = 0;
uint8_t demoPanelAddrs[30];

// ============================================================================
// Demo Effects
// ============================================================================

// All panels breathe warm orange together — one slow 4-second cycle.
void demoAllBreathe()
{
    PRINTLN("[DEMO] All Breathe");
    Protocol::ColorRGB warmOrange = {255, 80, 10};
    Protocol::Color c;
    c.rgb = warmOrange;
    for (uint8_t i = 0; i < DEMO_PANELS; i++) {
        panelsController->setColorAndBrightness(demoPanelAddrs[i], c, 0);
        panelsController->turnOn(demoPanelAddrs[i]);
    }
    animScheduler->playOnPanels(1, Lightnet::ANIM_BREATHE, 0, 4000,
        warmOrange, warmOrange, 0, 220, 0, 0,
        demoPanelAddrs, DEMO_PANELS);
    delay(4100);
}

// All panels cycle through the full rainbow, then fade out from whatever hue they land on.
void demoRainbow()
{
    PRINTLN("[DEMO] Rainbow");
    Protocol::ColorRGB black = {0, 0, 0};
    for (uint8_t i = 0; i < DEMO_PANELS; i++) {
        panelsController->turnOn(demoPanelAddrs[i]);
    }
    // speed=12 → one full hue cycle ≈ 1275 ms → ~5.5 cycles in 7 seconds.
    animScheduler->playOnPanels(2, Lightnet::ANIM_HUE_CYCLE, 0, 7000,
        black, black, 0, 200, 12, 0,
        demoPanelAddrs, DEMO_PANELS);
    delay(7000);

    // Fade out over 1 s from whatever hue the cycle landed on.
    uint8_t fadeFlags = Lightnet::FLAG_CURRENT_COLOR_TO | Lightnet::FLAG_CURRENT_COLOR_FROM | Lightnet::FLAG_CURRENT_BRIGHTNESS_FROM;
    animScheduler->playOnPanels(3, Lightnet::ANIM_FADE, fadeFlags, 1000,
        black, black, 0, 0, 0, 0,
        demoPanelAddrs, DEMO_PANELS);
    delay(1100);
}

// Panels fire a red pulse one at a time, 4 rounds.
void demoStaggeredPulse()
{
    PRINTLN("[DEMO] Staggered Pulse");
    Protocol::ColorRGB fire = {255, 30, 0};
    Protocol::ColorRGB black = {0, 0, 0};
    for (uint8_t rep = 0; rep < 4; rep++) {
        for (uint8_t i = 0; i < DEMO_PANELS; i++) {
            panelsController->turnOn(demoPanelAddrs[i]);
            // param1=51 → ~20% rise, param2=128 → ~50% fall, hold fills the rest
            animScheduler->playOnPanels(10 + i, Lightnet::ANIM_PULSE, 0, 600,
                black, fire, 0, 255, 51, 128,
                &demoPanelAddrs[i], 1);
            delay(250);
        }
        delay(500);
    }
}

// Cyan dot chases across panels — 4 passes, controller-computed.
void demoChaseLight()
{
    PRINTLN("[DEMO] Chase");
    Protocol::ColorRGB cyan = {0, 200, 255};
    Protocol::Color c;
    c.rgb = cyan;
    for (uint8_t i = 0; i < DEMO_PANELS; i++) {
        panelsController->setColorAndBrightness(demoPanelAddrs[i], c, 0);
        panelsController->turnOn(demoPanelAddrs[i]);
    }
    for (uint8_t pass = 0; pass < 4; pass++) {
        Lightnet::ChaseRunner runner(20 + pass, demoPanelAddrs, DEMO_PANELS, 1200, cyan);
        while (!runner.isFinished()) {
            runner.tick(millis());
            delay(8);
        }
    }
}

// Warm-white brightness wave sweeps across panels — 3 passes, controller-computed.
void demoColorWave()
{
    PRINTLN("[DEMO] Color Wave");
    Protocol::ColorRGB warm = {255, 220, 160};
    Protocol::Color c;
    c.rgb = warm;
    for (uint8_t i = 0; i < DEMO_PANELS; i++) {
        panelsController->setColorAndBrightness(demoPanelAddrs[i], c, 0);
        panelsController->turnOn(demoPanelAddrs[i]);
    }
    for (uint8_t pass = 0; pass < 3; pass++) {
        Lightnet::WaveRunner runner(30 + pass, demoPanelAddrs, DEMO_PANELS, 1500, 2, warm);
        while (!runner.isFinished()) {
            runner.tick(millis());
            delay(8);
        }
        delay(200);
    }
}

void runDemos()
{
    demoAllBreathe();
    delay(300);
    demoRainbow();
    delay(300);
    demoStaggeredPulse();
    delay(300);
    demoChaseLight();
    delay(300);
    demoColorWave();
    delay(300);
}

#endif // DEMO_ENABLED
#endif // LIGHTNET_TARGET_CONTROLLER
