// Verification demos — each one exercises a specific animation code path that
// previously had a confirmed bug. They use the scheduler + panel API directly
// rather than the scene pipeline, which keeps the verification as close to the
// original bug-hit surface as possible.
//
// They are kept in the demo rotation so a regression in any of the fixed paths
// shows up visually before unit tests exist.

#ifdef LIGHTNET_TARGET_CONTROLLER
#include "../config.hpp"   // brings in DEMO_MODE before the guard
#if DEMO_MODE

    #include "DemoRunner.hpp"
    #include "../../../lib/Lightnet/Utils/Debug.hpp"
    #include <Arduino.h>

    namespace Lightnet {
        // TESTS: tickBreathe lerp8 fix (brightnessTo < brightnessFrom).
        // Before fix: (brightnessTo - brightnessFrom) promoted to signed, then cast to
        // uint32_t ≈ 4.3B → garbage brightness. Should visibly pulse from bright to dim.
        void DemoRunner::demoInvertedBreathe()
        {
            DEBUG_IF(DEBUG_DEMO, D_PRINTLN("[DEMO] Inverted Breathe"));

            uint8_t addrs[LIGHTNET_MAX_PANELS];
            uint8_t n = resolvePanels(addrs, LIGHTNET_MAX_PANELS);
            Protocol::ColorRGB ice = { 60, 160, 255 };

            setAll(ice, 220);
            scheduler.playOnPanels(1, ANIM_BREATHE, FLAG_LOOP, 2000,
                                   demoDim(ice, 220), demoDim(ice, 15), 0, 0, addrs, n);
            wait(6200);
            turnOffAll();
        }

        // TESTS: PREPARE→START race fix (idle tick no longer drains the queue).
        // 10 back-to-back 280 ms TRANSITION calls — every cycle hit the race window.
        // All 10 transitions should complete on every panel with no silent skips.
        void DemoRunner::demoRapidTransitions()
        {
            DEBUG_IF(DEBUG_DEMO, D_PRINTLN("[DEMO] Rapid Transitions"));

            uint8_t addrs[LIGHTNET_MAX_PANELS];
            uint8_t n = resolvePanels(addrs, LIGHTNET_MAX_PANELS);
            static const Protocol::ColorRGB palette[] = {
                { 255, 20, 0 },
                { 0, 200, 50 },
                { 0, 80, 255 },
                { 255, 160, 0 },
                { 200, 0, 255 },
            };
            const uint8_t N = sizeof(palette) / sizeof(palette[0]);

            setAll(palette[0], 200);

            for (uint8_t step = 0; step < 10; step++) {
                scheduler.playOnPanels(10 + step, ANIM_TRANSITION, 0, 280,
                                       demoDim(palette[step % N], 200), demoDim(palette[(step + 1) % N], 200), 0, 0, addrs, n);
                wait(300);
            }

            turnOffAll();
        }

        // TESTS: tickPulse hold_pct clamp fix (rise + fall > 255).
        // params: rise=170, fall=110, sum=280. Before fix: hold_pct wrapped to 231,
        // making it look like a slow fade-in-and-hold. After fix: sharp crisp spike.
        void DemoRunner::demoExtremePulse()
        {
            DEBUG_IF(DEBUG_DEMO, D_PRINTLN("[DEMO] Extreme Pulse"));

            uint8_t addrs[LIGHTNET_MAX_PANELS];
            uint8_t n = resolvePanels(addrs, LIGHTNET_MAX_PANELS);
            Protocol::ColorRGB magenta = { 255, 0, 150 };
            Protocol::ColorRGB black   = { 0, 0, 0 };

            for (uint8_t i = 0; i < n; i++) panels.turnOn(addrs[i]);

            for (uint8_t rep = 0; rep < 7; rep++) {
                scheduler.playOnPanels(20 + rep, ANIM_PULSE, 0, 500,
                                       black, magenta, 170, 110, addrs, n);
                wait(520);
            }

            turnOffAll();
        }

        // TESTS: static→member lastBrightness fix in controller runners.
        // 3 WaveRunners then 3 RippleRunners — each a separate instance. Before fix,
        // lastBrightness[] was static and shared, so the second runner inherited stale
        // values and suppressed its opening I²C frames. Each pass must start cleanly.
        void DemoRunner::demoWaveRippleSequence()
        {
            DEBUG_IF(DEBUG_DEMO, D_PRINTLN("[DEMO] Wave + Ripple sequence"));

            uint8_t addrs[LIGHTNET_MAX_PANELS];
            uint8_t n = resolvePanels(addrs, LIGHTNET_MAX_PANELS);
            Protocol::ColorRGB warm = { 255, 180, 60 };
            Protocol::ColorRGB cool = { 0, 140, 255 };
            Protocol::Color c;

            c.rgb = warm;

            for (uint8_t i = 0; i < n; i++) {
                panels.setColor(addrs[i], demoColor(demoDim(c.rgb, 0)));
                panels.turnOn(addrs[i]);
            }

            for (uint8_t p = 0; p < 3; p++) {
                WaveRunner wave(30 + p, addrs, n, 1400, 2, warm);

                while (!wave.isFinished()) {
                    wave.tick(millis());
                    serviceMirror();
                    delay(8);
                }

                wait(150);
            }

            c.rgb = cool;

            for (uint8_t i = 0; i < n; i++) panels.setColor(addrs[i], demoColor(demoDim(c.rgb, 0)));

            for (uint8_t p = 0; p < 3; p++) {
                RippleRunner ripple(33 + p, addrs, n, p % n, 1400, 2, cool);

                while (!ripple.isFinished()) {
                    ripple.tick(millis());
                    serviceMirror();
                    delay(8);
                }

                wait(150);
            }

            turnOffAll();
        }

        // TESTS: tickReactive rgbLerp fix (colorFrom channels > colorTo channels).
        // ember={180,60,10}, flash={10,10,255}: R and G are inverted. Before fix,
        // (colorTo.r - colorFrom.r) promoted negative → garbage colour on every beat.
        void DemoRunner::demoReactiveBeats()
        {
            DEBUG_IF(DEBUG_DEMO, D_PRINTLN("[DEMO] Reactive Beats"));

            uint8_t addrs[LIGHTNET_MAX_PANELS];
            uint8_t n = resolvePanels(addrs, LIGHTNET_MAX_PANELS);
            Protocol::ColorRGB ember = { 180, 60, 10 };
            Protocol::ColorRGB flash = { 10, 10, 255 };

            setAll(ember, 25);
            scheduler.playOnPanels(40, ANIM_REACTIVE, 0, 0,
                                   demoDim(ember, 25), flash, 210, 0, addrs, n);
            wait(500);

            for (uint8_t beat = 0; beat < 8; beat++) {
                scheduler.triggerGroup(40, 255);
                wait(500);
            }

            wait(1200);
            turnOffAll();
        }

        // Panels cycle through a full rainbow then fade to black together.
        void DemoRunner::demoRainbowFade()
        {
            DEBUG_IF(DEBUG_DEMO, D_PRINTLN("[DEMO] Rainbow Fade"));

            uint8_t addrs[LIGHTNET_MAX_PANELS];
            uint8_t n = resolvePanels(addrs, LIGHTNET_MAX_PANELS);
            Protocol::ColorRGB black = { 0, 0, 0 };

            for (uint8_t i = 0; i < n; i++) panels.turnOn(addrs[i]);

            scheduler.playOnPanels(50, ANIM_HUE_CYCLE, 0, 5000,
                                   black, black, 10, 0, addrs, n);
            wait(5000);

            uint8_t fadeFlags = FLAG_CURRENT_COLOR_FROM | FLAG_CURRENT_COLOR_TO;

            scheduler.playOnPanels(51, ANIM_FADE, fadeFlags, 800,
                                   black, black, 0, 0, addrs, n);
            wait(900);
        }

        // Warm-white wave sweeps across 3 times, followed by a cyan single-panel chase.
        void DemoRunner::demoWaveAndChase()
        {
            DEBUG_IF(DEBUG_DEMO, D_PRINTLN("[DEMO] Wave + Chase"));

            uint8_t addrs[LIGHTNET_MAX_PANELS];
            uint8_t n = resolvePanels(addrs, LIGHTNET_MAX_PANELS);
            Protocol::ColorRGB warm = { 255, 210, 140 };
            Protocol::ColorRGB cyan = { 0, 200, 255 };
            Protocol::Color c;

            c.rgb = warm;

            for (uint8_t i = 0; i < n; i++) {
                panels.setColor(addrs[i], demoColor(demoDim(c.rgb, 0)));
                panels.turnOn(addrs[i]);
            }

            for (uint8_t p = 0; p < 3; p++) {
                WaveRunner wave(60 + p, addrs, n, 1200, 2, warm);

                while (!wave.isFinished()) {
                    wave.tick(millis());
                    serviceMirror();
                    delay(8);
                }

                wait(120);
            }

            c.rgb = cyan;

            for (uint8_t i = 0; i < n; i++) panels.setColor(addrs[i], demoColor(demoDim(c.rgb, 0)));

            for (uint8_t p = 0; p < 4; p++) {
                ChaseRunner chase(63 + p, addrs, n, 1000, cyan);

                while (!chase.isFinished()) {
                    chase.tick(millis());
                    serviceMirror();
                    delay(8);
                }
            }

            turnOffAll();
        }
    } // namespace Lightnet

#endif  // DEMO_MODE
#endif  // LIGHTNET_TARGET_CONTROLLER
