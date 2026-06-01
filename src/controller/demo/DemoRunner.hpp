#pragma once
#ifdef LIGHTNET_TARGET_CONTROLLER
    #if DEMO_ENABLED

        #include "../../../lib/Lightnet/Controller/Scenes/AnimationService.hpp"
        #include "../../../lib/Lightnet/Controller/Scenes/SceneStore.hpp"
        #include "../../../lib/Lightnet/Controller/Scenes/ScenePlayer.hpp"
        #include "../../../lib/Lightnet/Controller/Animations/AnimationScheduler.hpp"
        #include "../../../lib/Lightnet/Controller/Animations/AnimationRunner.hpp"
        #include "../../../lib/Lightnet/Controller/Panels/PanelsController.hpp"
        #include "../../../lib/Lightnet/Controller/Panels/PanelsInitializer.hpp"

        namespace Lightnet {
            // DemoRunner owns the full demo cycle. It holds no global state — all resources
            // are injected. Two modes of demo coexist in the same rotation:
            //
            //   Verification demos  (demos_checks.cpp) — legacy, use scheduler + panels
            //     directly to exercise specific animation code paths.
            //
            //   Scene demos         (demos_scenes.cpp) — use AnimationService to save and play
            //     real scene files, demonstrating the full pipeline.
            //
            // Usage:
            //   runner.seedScenes();   // once after SPIFFS mount — writes scene files if missing
            //   runner.run();          // called from loop — runs one demo then returns
            //
            class DemoRunner
            {
                public:
                    DemoRunner(
                        AnimationService&   animService,
                        SceneStore&         sceneStore,
                        ScenePlayer&        scenePlayer,
                        AnimationScheduler& scheduler,
                        PanelsController&   panels,
                        PanelsInitializer&  initializer
                    );

                    // Write all scene-demo JSON files to SPIFFS once (skips files that already exist).
                    // Call after SPIFFS.begin() and before the first run().
                    void seedScenes();

                    // Run the next demo in the rotation. Blocks until the demo completes.
                    void run();

                private:
                    AnimationService& animService;
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

                    // Save `json` to SPIFFS as scene `name` — only if the file doesn't exist yet.
                    void seedScene(const char *name, const char *json);

                    // Play a stored scene for `durationMs`, ticking ScenePlayer internally.
                    void playScene(const char *name, uint32_t durationMs);

                    // Spin-wait for `ms` milliseconds, calling scenePlayer.tick() every 16 ms.
                    void spinWait(uint32_t ms);

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

    #endif  // DEMO_ENABLED
#endif  // LIGHTNET_TARGET_CONTROLLER
