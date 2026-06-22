// Scene-based demos — each one saves a named scene to the filesystem (once, on first
// boot) and plays it via ScenesService. Group IDs are in the 100+ range so
// they never collide with user scenes or the verification demos.
//
// Scenes are kept permanently on the filesystem so they are also reachable via HTTP
// after boot: POST /api/scenes/demo_warm_breathe/play, etc.

#ifdef LIGHTNET_TARGET_CONTROLLER
#include "../config.hpp"   // brings in DEMO_MODE before the guard
#if DEMO_MODE

    #include "DemoRunner.hpp"
    #include "../../../lib/Lightnet/Utils/Debug.hpp"
    #include "../../../lib/Lightnet/Utils/EntryId.hpp"
    #include <Arduino.h>
    #include <string.h>

    namespace Lightnet {
        // ============================================================================
        // Scene JSON definitions
        // Each must parse cleanly through parseScene() — validated at seed time.
        // ============================================================================

        // Single-layer slow breathe. embers palette, palette pos 200 ≈ bright orange.
        static const char SCENE_WARM_BREATHE[] =
            "{\"schemaVersion\":1,\"name\":\"demo_warm_breathe\",\"loop\":true,"
            "\"palette\":\"embers\","
            "\"layers\":[{\"group\":101,\"panels\":\"all\","
            "\"sequence\":[{\"type\":\"BREATHE\","
            "\"color\":{\"palette\":200},"
            "\"brightnessFrom\":15,\"brightnessTo\":220,"
            "\"duration\":30000,\"loop\":true}]}]}";

        // Dim lava background (group 102) + wave runner sweeping over it (group 103).
        // Both layers share a 2400 ms cycle so they restart in sync.
        static const char SCENE_LAVA_WAVE[] =
            "{\"schemaVersion\":1,\"name\":\"demo_lava_wave\",\"loop\":true,"
            "\"palette\":\"lava\","
            "\"layers\":["
            "{\"group\":102,\"panels\":\"all\","
            "\"sequence\":[{\"type\":\"SOLID\","
            "\"color\":{\"palette\":80},\"brightnessTo\":35,\"duration\":2400}]},"
            "{\"group\":103,\"panels\":\"all\","
            "\"sequence\":["
            "{\"runner\":\"WAVE\",\"color\":{\"palette\":220},"
            "\"duration\":2000,\"params\":[3]},"
            "{\"type\":\"SOLID\",\"color\":{\"palette\":80},"
            "\"brightnessTo\":35,\"duration\":400}]}]}";

        // Three-step arc: fade in from black → long breathe → fade to black. Loops.
        static const char SCENE_SUNSET_SEQ[] =
            "{\"schemaVersion\":1,\"name\":\"demo_sunset_seq\",\"loop\":true,"
            "\"palette\":\"sunset\","
            "\"colors\":{\"primary\":\"#FF4400\",\"secondary\":\"#FF8800\",\"tertiary\":\"#441100\"},"
            "\"layers\":[{\"group\":104,\"panels\":\"all\","
            "\"sequence\":["
            "{\"type\":\"TRANSITION\","
            "\"colorFrom\":{\"palette\":0},\"colorTo\":{\"palette\":180},"
            "\"brightnessFrom\":0,\"brightnessTo\":200,\"duration\":2000},"
            "{\"type\":\"BREATHE\","
            "\"color\":{\"palette\":180},"
            "\"brightnessFrom\":70,\"brightnessTo\":220,"
            "\"duration\":4000,\"loop\":true},"
            "{\"type\":\"FADE\","
            "\"color\":{\"palette\":180},"
            "\"brightnessFrom\":200,\"brightnessTo\":0,\"duration\":1500}"
            "]}]}";

        // REACTIVE on embers palette — DemoRunner triggers beats manually so this demo
        // simulates music sync. Duration 0 = infinite (valid: only/last step, loop=true).
        static const char SCENE_FIRE_REACT[] =
            "{\"schemaVersion\":1,\"name\":\"demo_fire_react\",\"loop\":true,"
            "\"palette\":\"embers\","
            "\"layers\":[{\"group\":105,\"panels\":\"all\","
            "\"sequence\":[{\"type\":\"REACTIVE\","
            "\"colorFrom\":{\"palette\":30},\"colorTo\":{\"palette\":220},"
            "\"brightnessFrom\":20,\"brightnessTo\":255,"
            "\"duration\":0,\"params\":[210]}]}]}";

        // Dim aurora breathe (group 106, 60 s period — effectively infinite for demo)
        // + chase runner cycling over it (group 107, 2800 ms cycle).
        static const char SCENE_AURORA_CHASE[] =
            "{\"schemaVersion\":1,\"name\":\"demo_aurora_chase\",\"loop\":true,"
            "\"palette\":\"aurora\","
            "\"layers\":["
            "{\"group\":106,\"panels\":\"all\","
            "\"sequence\":[{\"type\":\"BREATHE\","
            "\"color\":{\"palette\":60},"
            "\"brightnessFrom\":10,\"brightnessTo\":80,"
            "\"duration\":60000,\"loop\":true}]},"
            "{\"group\":107,\"panels\":\"all\","
            "\"sequence\":["
            "{\"runner\":\"CHASE\",\"color\":{\"palette\":220},\"duration\":2500},"
            "{\"type\":\"SOLID\",\"color\":{\"palette\":60},"
            "\"brightnessTo\":15,\"duration\":300}]}]}";

        // Three RIPPLE runners at different rainbow hues (red/green/blue) from origin
        // panel 0, then a 400 ms black gap. Demonstrates sequential runner steps.
        static const char SCENE_RIPPLE_CASC[] =
            "{\"schemaVersion\":1,\"name\":\"demo_ripple_casc\",\"loop\":true,"
            "\"palette\":\"rainbow\","
            "\"layers\":[{\"group\":108,\"panels\":\"all\","
            "\"sequence\":["
            "{\"runner\":\"RIPPLE\",\"color\":{\"palette\":0},"
            "\"duration\":1500,\"params\":[2,0]},"
            "{\"runner\":\"RIPPLE\",\"color\":{\"palette\":85},"
            "\"duration\":1500,\"params\":[2,0]},"
            "{\"runner\":\"RIPPLE\",\"color\":{\"palette\":170},"
            "\"duration\":1500,\"params\":[2,0]},"
            "{\"type\":\"SOLID\",\"color\":{\"palette\":0},"
            "\"brightnessTo\":0,\"duration\":400}"
            "]}]}";

        // ============================================================================
        // Seed
        // ============================================================================

        void DemoRunner::seedScenes()
        {
            seedScene("demo_warm_breathe", SCENE_WARM_BREATHE);
            seedScene("demo_lava_wave", SCENE_LAVA_WAVE);
            seedScene("demo_sunset_seq", SCENE_SUNSET_SEQ);
            seedScene("demo_fire_react", SCENE_FIRE_REACT);
            seedScene("demo_aurora_chase", SCENE_AURORA_CHASE);
            seedScene("demo_ripple_casc", SCENE_RIPPLE_CASC);
        }

        // ============================================================================
        // Demo functions
        // ============================================================================

        void DemoRunner::demoWarmBreathe()
        {
            DEBUG_IF(DEBUG_DEMO, D_PRINTLN("[DEMO] Warm Breathe"));
            playScene("demo_warm_breathe", 9000); // ~3 breathe cycles at 3 s period
        }

        void DemoRunner::demoLavaWave()
        {
            DEBUG_IF(DEBUG_DEMO, D_PRINTLN("[DEMO] Lava Wave"));
            playScene("demo_lava_wave", 9600); // 4 × 2400 ms wave cycles
        }

        void DemoRunner::demoSunsetSequence()
        {
            DEBUG_IF(DEBUG_DEMO, D_PRINTLN("[DEMO] Sunset Sequence"));
            playScene("demo_sunset_seq", 15000); // 2 full sequences (7500 ms each)
        }

        void DemoRunner::demoFireReactive()
        {
            DEBUG_IF(DEBUG_DEMO, D_PRINTLN("[DEMO] Fire Reactive"));

            // Play the reactive scene, then manually fire beat triggers so the demo
            // works without a WebSocket client. ~100 BPM for 8 seconds.
            char seed[40];
            char id[sizeof(SceneMeta::id)];

            snprintf(seed, sizeof(seed), "demo:%s", "demo_fire_react");
            deterministicId(seed, id, sizeof(id));

            auto r = animService.playSceneById(id);

            if (!r.ok()) {
                DEBUG_IF(DEBUG_DEMO, D_PRINTFLN("[DEMO] fire_react play failed: %s", r.msg));

                return;
            }

            wait(500); // let the animation arm before the first beat

            uint32_t deadline  = millis() + 8000;
            uint32_t nextBeat  = millis() + 600;

            while ((int32_t)(millis() - deadline) < 0) {
                uint32_t now = millis();

                if ((int32_t)(now - nextBeat) >= 0) {
                    scheduler.triggerGroup(105, 255); // group 105 = fire_react layer
                    nextBeat = now + 600;      // ~100 BPM
                }

                scenePlayer.tick(now);
                serviceMirror();
                delay(16);
            }

            animService.stopScene();
            wait(500);
        }

        void DemoRunner::demoAuroraChase()
        {
            DEBUG_IF(DEBUG_DEMO, D_PRINTLN("[DEMO] Aurora Chase"));
            playScene("demo_aurora_chase", 11200); // 4 × 2800 ms chase cycles
        }

        void DemoRunner::demoRippleCascade()
        {
            DEBUG_IF(DEBUG_DEMO, D_PRINTLN("[DEMO] Ripple Cascade"));
            playScene("demo_ripple_casc", 10400); // 2 full ripple cycles (5200 ms each)
        }
    } // namespace Lightnet

#endif  // DEMO_MODE
#endif  // LIGHTNET_TARGET_CONTROLLER
