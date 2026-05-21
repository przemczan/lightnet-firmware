#ifdef LIGHTNET_TARGET_CONTROLLER
#if DEMO_ENABLED

#include "DemoRunner.hpp"
#include "../../../lib/Lightnet/Utils/Debug.hpp"
#include <Arduino.h>

namespace Lightnet {

// ============================================================================
// Construction
// ============================================================================

DemoRunner::DemoRunner(AnimationService&   _animService,
                        SceneStore&         _sceneStore,
                        ScenePlayer&        _scenePlayer,
                        AnimationScheduler& _scheduler,
                        PanelsController&   _panels,
                        PanelsInitializer&  _initializer)
    : animService(_animService), sceneStore(_sceneStore), scenePlayer(_scenePlayer),
      scheduler(_scheduler), panels(_panels), initializer(_initializer), index(0) {}

// ============================================================================
// Demo dispatch
// ============================================================================

void DemoRunner::run()
{
    PRINTKV("[DEMO] running demo", (int)index);
    switch (index) {
        // -- Verification demos (exercise specific animation code paths) --
        case  0: demoInvertedBreathe();    break;
        case  1: demoRapidTransitions();   break;
        case  2: demoExtremePulse();       break;
        case  3: demoWaveRippleSequence(); break;
        case  4: demoReactiveBeats();      break;
        case  5: demoRainbowFade();        break;
        case  6: demoWaveAndChase();       break;
        // -- Scene-based demos (full pipeline: AnimationService → ScenePlayer) --
        case  7: demoWarmBreathe();        break;
        case  8: demoLavaWave();           break;
        case  9: demoSunsetSequence();     break;
        case 10: demoFireReactive();       break;
        case 11: demoAuroraChase();        break;
        case 12: demoRippleCascade();      break;
    }
    index = (index + 1) % 13;
}

// ============================================================================
// Helpers
// ============================================================================

uint8_t DemoRunner::resolvePanels(uint8_t* out, uint8_t maxCount) const
{
    List<Panel*>* list = initializer.getPanels();
    uint16_t total = list->getSize();
    uint8_t count = 0;
    for (uint16_t i = 0; i < total && count < maxCount; i++) {
        out[count++] = (uint8_t)list->get(i)->index;
    }
    return count;
}

void DemoRunner::setAll(Protocol::ColorRGB rgb, uint8_t brightness)
{
    uint8_t addrs[3];
    uint8_t count = resolvePanels(addrs, 3);
    Protocol::Color c;
    c.rgb = rgb;
    for (uint8_t i = 0; i < count; i++) {
        panels.setColorAndBrightness(addrs[i], c, brightness);
        panels.turnOn(addrs[i]);
    }
}

void DemoRunner::turnOffAll()
{
    uint8_t addrs[3];
    uint8_t count = resolvePanels(addrs, 3);
    for (uint8_t i = 0; i < count; i++) {
        panels.turnOff(addrs[i]);
    }
    delay(500);
}

void DemoRunner::seedScene(const char* name, const char* json)
{
    if (!sceneStore.exists(name)) {
        auto r = animService.saveScene(json, strlen(json));
        if (!r.ok()) {
            PRINTF("[DEMO] seed failed for %s: %s\n", name, r.msg);
        }
    }
}

void DemoRunner::playScene(const char* name, uint32_t durationMs)
{
    auto r = animService.playSceneByName(name);
    if (!r.ok()) {
        PRINTF("[DEMO] play failed for %s: %s\n", name, r.msg);
        return;
    }
    spinWait(durationMs);
    animService.stopScene();
}

void DemoRunner::spinWait(uint32_t ms)
{
    uint32_t deadline = millis() + ms;
    while ((int32_t)(millis() - deadline) < 0) {
        scenePlayer.tick(millis());
        delay(16);
    }
}

}  // namespace Lightnet

#endif  // DEMO_ENABLED
#endif  // LIGHTNET_TARGET_CONTROLLER
