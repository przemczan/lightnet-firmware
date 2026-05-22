#include "ScenePlayer.hpp"
#include "../Animations/AnimationRunner.hpp"
#include "../../Utils/Debug.hpp"
#include <string.h>
#include <Arduino.h>

namespace Lightnet {
    ScenePlayer::ScenePlayer(
        AnimationScheduler& _scheduler,
        PanelsInitializer&  _initializer,
        PaletteStore&       _paletteStore
    )
        : scheduler(_scheduler), initializer(_initializer), paletteStore(_paletteStore),
        lCount(0), loop(false), playing(false)
    {
        memset(name, 0, sizeof(name));
        memset(defaultPalette, 0, sizeof(defaultPalette));
        memset(baseColors, 0, sizeof(baseColors));
        memset(currentStep, 0, sizeof(currentStep));
        memset(stepStartMs, 0, sizeof(stepStartMs));
        memset(layerActive, 0, sizeof(layerActive));
    }

    void ScenePlayer::loadAndPlay(
        const SceneLayer *       newLayers,
        uint8_t                  newCount,
        bool                     newLoop,
        const char *             newName,
        const char *             paletteDefault,
        const Protocol::ColorRGB newBaseColors[BASE_COLORS_COUNT],
        uint32_t                 nowMs
    )
    {
        stop();

        lCount = (newCount > SCENE_MAX_LAYERS) ? SCENE_MAX_LAYERS : newCount;
        memcpy(layers, newLayers, lCount * sizeof(SceneLayer));
        loop = newLoop;

        strncpy(name, newName ? newName : "", sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';

        const char *pal = (paletteDefault && paletteDefault[0]) ? paletteDefault : "userColors";

        strncpy(defaultPalette, pal, sizeof(defaultPalette) - 1);
        defaultPalette[sizeof(defaultPalette) - 1] = '\0';

        memcpy(baseColors, newBaseColors, sizeof(baseColors));

        // Pre-resolve palette for each layer
        for (uint8_t i = 0; i < lCount; i++) {
            const char *palName = (layers[i].palette[0]) ? layers[i].palette : defaultPalette;

            if (strcmp(palName, "userColors") == 0) {
                PaletteStore::buildUserColors(baseColors, resolvedPalettes[i], resolvedPaletteCounts[i]);
            } else if (!paletteStore.resolve(palName, resolvedPalettes[i], resolvedPaletteCounts[i])) {
                PaletteStore::buildUserColors(baseColors, resolvedPalettes[i], resolvedPaletteCounts[i]);
            }
        }

        // Clear any stale queued animations from the previous scene so new PREPAREs
        // are never dropped due to a full 4-slot queue on the panel side.
        scheduler.clearAllPanelQueues();

        sendPalettesToPanels();

        // Initialise step tracking and fire first step
        for (uint8_t i = 0; i < lCount; i++) {
            currentStep[i] = 0;
            stepStartMs[i] = nowMs;
            layerActive[i] = (layers[i].stepCount > 0);
        }

        playing = true;

        for (uint8_t i = 0; i < lCount; i++) {
            if (layerActive[i]) fireStep(i, nowMs);
        }
    }

    void ScenePlayer::stop()
    {
        playing = false;
        memset(layerActive, 0, sizeof(layerActive));
    }

    void ScenePlayer::tick(uint32_t nowMs)
    {
        if (!playing) return;

        bool anyActive = false;

        for (uint8_t i = 0; i < lCount; i++) {
            if (!layerActive[i]) continue;

            anyActive = true;

            const SceneStep& step = layers[i].steps[currentStep[i]];

            if (step.durationMs == 0) continue; // infinite — never auto-advance

            uint32_t elapsed = (uint32_t)(nowMs - stepStartMs[i]);

            if (elapsed < (uint32_t)step.durationMs) continue;

            uint8_t nextStep = currentStep[i] + 1;

            if (nextStep < layers[i].stepCount) {
                currentStep[i] = nextStep;
                stepStartMs[i] = nowMs;
                fireStep(i, nowMs);
            } else if (loop) {
                currentStep[i] = 0;
                stepStartMs[i] = nowMs;
                fireStep(i, nowMs);
            } else {
                layerActive[i] = false;
            }
        }

        if (!anyActive) playing = false;
    }

    void ScenePlayer::fireStep(uint8_t layerIdx, uint32_t /*nowMs*/)
    {
        const SceneLayer& layer = layers[layerIdx];
        const SceneStep& step  = layer.steps[currentStep[layerIdx]];

        uint8_t panels[SCENE_MAX_PANELS_PER_LAYER];
        uint8_t panelCount = 0;

        resolvePanels(layer, panels, panelCount);

        if (panelCount == 0) return;

        if (isRunnerType(step.animType)) {
            Protocol::ColorRGB color = resolveColorToRgb(step.colorFrom, layerIdx);
            AnimationRunner *runner = nullptr;

            if (step.animType == RUN_WAVE) {
                runner = new WaveRunner(layer.groupId, panels, panelCount,
                                        step.durationMs, step.params[0], color);
            } else if (step.animType == RUN_RIPPLE) {
                runner = new RippleRunner(layer.groupId, panels, panelCount,
                                          step.params[1], step.durationMs,
                                          step.params[0], color);
            } else if (step.animType == RUN_CHASE) {
                runner = new ChaseRunner(layer.groupId, panels, panelCount,
                                         step.durationMs, color);
            }

            if (runner) scheduler.addRunner(runner);
        } else {
            scheduler.playOnPanels(layer.groupId, step.animType, step.flags,
                                   step.durationMs,
                                   step.colorFrom, step.colorTo,
                                   step.brightnessFrom, step.brightnessTo,
                                   step.params[0], step.params[1],
                                   panels, panelCount);
        }
    }

    void ScenePlayer::resolvePanels(const SceneLayer& layer, uint8_t *out, uint8_t& count) const
    {
        List<Panel *> *allPanels = initializer.getPanels();
        uint16_t total = allPanels->getSize();

        count = 0;

        if (layer.targetMode == PanelTargetMode::ALL) {
            for (uint16_t i = 0; i < total && count < SCENE_MAX_PANELS_PER_LAYER; i++) {
                out[count++] = (uint8_t)allPanels->get(i)->index;
            }
        } else if (layer.targetMode == PanelTargetMode::LIST) {
            count = layer.targetCount;
            memcpy(out, layer.targetList, count);
        } else { // EXCLUDE
            for (uint16_t i = 0; i < total && count < SCENE_MAX_PANELS_PER_LAYER; i++) {
                uint8_t addr = (uint8_t)allPanels->get(i)->index;
                bool excluded = false;

                for (uint8_t j = 0; j < layer.targetCount; j++) {
                    if (layer.targetList[j] == addr) {
                        excluded = true;
                        break;
                    }
                }

                if (!excluded) out[count++] = addr;
            }
        }
    }

    void ScenePlayer::sendPalettesToPanels()
    {
        scheduler.broadcastBaseColors(baseColors);

        for (uint8_t i = 0; i < lCount; i++) {
            uint8_t panels[SCENE_MAX_PANELS_PER_LAYER];
            uint8_t panelCount = 0;

            resolvePanels(layers[i], panels, panelCount);

            if (panelCount == 0) continue;

            // Panels may be in an isOn=false state (e.g. after selfTest or an
            // explicit turn-off). Turn them on now so animation output is visible.
            scheduler.turnOnPanels(panels, panelCount);

            scheduler.unicastPaletteToPanels(resolvedPalettes[i], resolvedPaletteCounts[i],
                                             panels, panelCount);
        }
    }

    Protocol::ColorRGB ScenePlayer::resolveColorToRgb(const ColorRef& ref, uint8_t layerIdx) const
    {
        if (ref.kind == COLORREF_RGB) {
            return { ref.rgb.r, ref.rgb.g, ref.rgb.b };
        }

        if (ref.kind == COLORREF_PALETTE) {
            uint8_t r = 255, g = 255, b = 255;

            samplePalette(resolvedPalettes[layerIdx], resolvedPaletteCounts[layerIdx],
                          ref.palette.pos, &r, &g, &b);

            return { r, g, b };
        }

        if (ref.kind == COLORREF_USE_COLOR) {
            uint8_t slot = ref.useColor.slot;

            if (slot < BASE_COLORS_COUNT) return baseColors[slot];
        }

        return { 255, 255, 255 };
    }

    void ScenePlayer::writeStatusJson(char *buf, size_t bufLen) const
    {
        if (!playing) {
            snprintf(buf, bufLen, "{\"playing\":false}");
        } else {
            snprintf(buf, bufLen,
                     "{\"playing\":true,\"scene\":\"%s\",\"loop\":%s,\"layers\":%u}",
                     name, loop ? "true" : "false", (unsigned)lCount);
        }
    }
}  // namespace Lightnet
