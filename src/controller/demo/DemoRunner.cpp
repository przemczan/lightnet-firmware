#ifdef LIGHTNET_TARGET_CONTROLLER
#include "../config.hpp"   // brings in DEMO_MODE before the guard
#if DEMO_MODE

    #include "DemoRunner.hpp"
    #include "../../../lib/Lightnet/Utils/Debug.hpp"
    #include <Arduino.h>

    namespace Lightnet {
        // ============================================================================
        // Construction
        // ============================================================================

        DemoRunner::DemoRunner(
            ScenesService&      _animService,
            SceneStore&         _sceneStore,
            ScenePlayer&        _scenePlayer,
            AnimationScheduler& _scheduler,
            PanelsController&   _panels,
            PanelsInitializer&  _initializer
        )
            : animService(_animService), sceneStore(_sceneStore), scenePlayer(_scenePlayer),
            scheduler(_scheduler), panels(_panels), initializer(_initializer), index(0)
        {
        }

        // ============================================================================
        // Demo dispatch
        // ============================================================================

        void DemoRunner::run()
        {
            if (index == 0) Serial.println("[SIM:DEMO] start");

            DEBUG_IF(DEBUG_DEMO, D_PRINTLN("[DEMO] running demo", (int)index));

            switch (index) {
                // -- Verification demos (exercise specific animation code paths) --
                case  0: demoInvertedBreathe();
                    break;
                case  1: demoRapidTransitions();
                    break;
                case  2: demoExtremePulse();
                    break;
                case  3: demoWaveRippleSequence();
                    break;
                case  4: demoReactiveBeats();
                    break;
                case  5: demoRainbowFade();
                    break;
                case  6: demoWaveAndChase();
                    break;
                // -- Scene-based demos (full pipeline: ScenesService → ScenePlayer) --
                case  7: demoWarmBreathe();
                    break;
                case  8: demoLavaWave();
                    break;
                case  9: demoSunsetSequence();
                    break;
                case 10: demoFireReactive();
                    break;
                case 11: demoAuroraChase();
                    break;
                case 12: demoRippleCascade();
                    break;
            }

            index = (index + 1) % 13;

            if (index == 0) Serial.println("[SIM:DEMO] end");
        }

        // ============================================================================
        // Helpers
        // ============================================================================

        uint8_t DemoRunner::resolvePanels(uint8_t *out, uint8_t maxCount) const
        {
            List<Panel *> *list = initializer.getPanels();
            uint16_t total = list->getSize();
            uint8_t count = 0;

            for (uint16_t i = 0; i < total && count < maxCount; i++) {
                out[count++] = (uint8_t)list->get(i)->index;
            }

            return count;
        }

        void DemoRunner::setAll(Protocol::ColorRGB rgb, uint8_t brightness)
        {
            uint8_t addrs[LIGHTNET_MAX_PANELS];
            uint8_t count = resolvePanels(addrs, LIGHTNET_MAX_PANELS);
            Protocol::Color c = demoColor(demoDim(rgb, brightness));

            for (uint8_t i = 0; i < count; i++) {
                panels.setColor(addrs[i], c);
                panels.turnOn(addrs[i]);
            }
        }

        void DemoRunner::turnOffAll()
        {
            uint8_t addrs[LIGHTNET_MAX_PANELS];
            uint8_t count = resolvePanels(addrs, LIGHTNET_MAX_PANELS);

            for (uint8_t i = 0; i < count; i++) {
                panels.turnOff(addrs[i]);
            }

            wait(500);
        }

        void DemoRunner::seedScene(const char *name, const char *json)
        {
            char seed[40];
            char id[sizeof(SceneMeta::id)];

            snprintf(seed, sizeof(seed), "demo:%s", name);
            deterministicId(seed, id, sizeof(id));

            if (sceneStore.exists(id)) return;

            SceneRecord parsed = {};
            char errMsg[64];

            if (!parseScene(json, strlen(json), parsed, errMsg, sizeof(errMsg))) {
                DEBUG_IF(DEBUG_DEMO, D_PRINTF("[DEMO] seed parse failed for %s: %s\n", name, errMsg));

                return;
            }

            strncpy(parsed.id, id, sizeof(parsed.id) - 1);
            parsed.duration = computeSceneDurationMs(parsed);
            parsed.hidden   = 0;

            if (sceneStore.create(parsed) != SCENE_STORE_OK) {
                DEBUG_IF(DEBUG_DEMO, D_PRINTF("[DEMO] seed failed for %s\n", name));
            }
        }

        void DemoRunner::playScene(const char *name, uint32_t durationMs)
        {
            char seed[40];
            char id[sizeof(SceneMeta::id)];

            snprintf(seed, sizeof(seed), "demo:%s", name);
            deterministicId(seed, id, sizeof(id));

            auto r = animService.playSceneById(id);

            if (!r.ok()) {
                DEBUG_IF(DEBUG_DEMO, D_PRINTF("[DEMO] play failed for %s: %s\n", name, r.msg));

                return;
            }

            spinWait(durationMs);
            animService.stopScene();
        }

        void DemoRunner::spinWait(uint32_t ms)
        {
            uint32_t deadline = millis() + ms;

            while ((int32_t)(millis() - deadline) < 0) {
                // Both ticks, mirroring the main loop: scenePlayer advances steps, scheduler
                // drives the controller-side runners (WAVE/RIPPLE/CHASE) that scene layers add.
                scheduler.tick(millis());
                scenePlayer.tick(millis());
                serviceMirror();
                delay(16);
            }
        }

        void DemoRunner::wait(uint32_t ms)
        {
            uint32_t deadline = millis() + ms;

            while ((int32_t)(millis() - deadline) < 0) {
                serviceMirror();
                delay(8);
            }
        }
    } // namespace Lightnet

#endif  // DEMO_MODE
#endif  // LIGHTNET_TARGET_CONTROLLER
