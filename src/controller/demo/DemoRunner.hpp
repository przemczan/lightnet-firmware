#pragma once
#ifdef LIGHTNET_TARGET_CONTROLLER
    #if DEMO_MODE

        #include "../../../lib/Lightnet/Controller/Scenes/ScenesService.hpp"
        #include "../../../lib/Lightnet/Controller/Scenes/Store/SceneStore.hpp"
        #include "../../../lib/Lightnet/Utils/EntryId.hpp"
        #include "../../../lib/Lightnet/Core/Controller/ScenePlayer.hpp"
        #include "../../../lib/Lightnet/Core/Controller/AnimationScheduler.hpp"
        #include "../../../lib/Lightnet/Core/Controller/CompiledSweep.hpp"
        #include "../../../lib/Lightnet/Controller/Panels/PanelsController.hpp"
        #include "../../../lib/Lightnet/Controller/Panels/PanelsInitializer.hpp"
        #include "../MirrorService.hpp"   // serviceMirror() — keeps live preview streaming during demos

        namespace Lightnet {
            // Protocol v5 removed per-panel brightness — animations express brightness through
            // color. These fold a legacy 0–255 brightness into the color so the ported demos keep
            // their original bright→dim intent.
            static inline Protocol::ColorRGB demoDim(Protocol::ColorRGB c, uint8_t brightness)
            {
                return Protocol::ColorRGB{
                    (uint8_t)((uint16_t)c.r * brightness / 255),
                    (uint8_t)((uint16_t)c.g * brightness / 255),
                    (uint8_t)((uint16_t)c.b * brightness / 255),
                };
            }

            static inline Protocol::Color demoColor(Protocol::ColorRGB rgb)
            {
                Protocol::Color c;

                c.rgb = rgb;

                return c;
            }

            // DemoRunner owns the full demo cycle. It holds no global state — all resources
            // are injected. Two modes of demo coexist in the same rotation:
            //
            //   Verification demos  (demos_checks.cpp) — legacy, use scheduler + panels
            //     directly to exercise specific animation code paths.
            //
            //   Scene demos         (demos_scenes.cpp) — use ScenesService to save and play
            //     real scene files, demonstrating the full pipeline.
            //
            // Usage:
            //   runner.seedScenes();   // once after the filesystem mount — writes scene files if missing
            //   runner.run();          // called from loop — runs one demo then returns
            //
            class DemoRunner
            {
                public:
                    DemoRunner(
                        ScenesService&      animService,
                        SceneStore&         sceneStore,
                        ScenePlayer&        scenePlayer,
                        AnimationScheduler& scheduler,
                        PanelsController&   panels,
                        PanelsInitializer&  initializer
                    );

                    // Write all scene-demo JSON files to the filesystem once (skips files that already exist).
                    // Call after the filesystem.begin() and before the first run().
                    void seedScenes();

                    // Run the next demo in the rotation. Blocks until the demo completes.
                    void run();

                private:
                    ScenesService& animService;
                    SceneStore& sceneStore;
                    ScenePlayer& scenePlayer;
                    AnimationScheduler& scheduler;
                    PanelsController& panels;
                    PanelsInitializer& initializer;

                    uint8_t index;

                    // ---- Helpers ----

                    // Resolve up to maxCount panel I²C addresses from the initializer.
                    uint8_t resolvePanels(uint8_t *out, uint8_t maxCount) const;

                    // Set all panels (up to 3) to a solid colour and turn them on.
                    void setAll(Protocol::ColorRGB rgb, uint8_t brightness);

                    // Turn off all panels (up to 3) and wait 500 ms.
                    void turnOffAll();

                    // Save `json` to the filesystem as scene `name` — only if the file doesn't exist yet.
                    void seedScene(const char *name, const char *json);

                    // Play a stored scene for `durationMs`, ticking ScenePlayer internally.
                    void playScene(const char *name, uint32_t durationMs);

                    // Spin-wait for `ms` milliseconds, calling scenePlayer.tick() every 16 ms.
                    void spinWait(uint32_t ms);

                    // Blocking wait that keeps the mirror stream alive — pumps serviceMirror()
                    // every ~8 ms instead of a dead delay(), so demos appear live in the app.
                    void wait(uint32_t ms);

                    // ---- Verification demos (demos_checks.cpp) ----
                    void demoInvertedBreathe();
                    void demoRapidTransitions();
                    void demoExtremePulse();
                    void demoWaveRippleSequence();
                    void demoReactiveBeats();
                    void demoRainbowFade();
                    void demoWaveAndChase();

                    // ---- Scene demos (demos_scenes.cpp) ----
                    void demoWarmBreathe();
                    void demoLavaWave();
                    void demoSunsetSequence();
                    void demoFireReactive();
                    void demoAuroraChase();
                    void demoRippleCascade();
            };
        } // namespace Lightnet

    #endif  // DEMO_MODE
#endif  // LIGHTNET_TARGET_CONTROLLER
