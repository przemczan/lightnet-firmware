#include "ScenePlayer.hpp"
#include "../Animations/AnimationRunner.hpp"
#include "../Topology/PanelField.hpp"
#include "../Topology/PanelGeometry.hpp"
#include "../../Utils/Debug.hpp"
#include <string.h>
#include <Arduino.h>

#if DEBUG_SCENE
    static const char *animTypeName(uint8_t t)
    {
        switch (t) {
            case 0:  return "SOLID";
            case 1:  return "FADE";
            case 2:  return "TRANSITION";
            case 3:  return "BREATHE";
            case 4:  return "PULSE";
            case 5:  return "BLINK";
            case 6:  return "HUE_CYCLE";
            case 7:  return "STROBE";
            case 8:  return "REACTIVE";
            case 9:  return "GAP";
            case 64: return "WAVE";
            case 65: return "RIPPLE";
            case 66: return "CHASE";
            default: return "?";
        }
    }

#endif

namespace Lightnet {
    ScenePlayer::ScenePlayer(
        AnimationScheduler& _scheduler,
        PanelsInitializer&  _initializer,
        PaletteStore&       _paletteStore
    )
        : scheduler(_scheduler), initializer(_initializer), paletteStore(_paletteStore),
        logicalRoot(1), tagResolver(nullptr),
        lCount(0), loop(false), playing(false), speed(1.0f)
    {
        memset(name, 0, sizeof(name));
        memset(defaultPalette, 0, sizeof(defaultPalette));
        memset(baseColors, 0, sizeof(baseColors));
        memset(currentStep, 0, sizeof(currentStep));
        memset(stepStartMs, 0, sizeof(stepStartMs));
        memset(layerState, 0, sizeof(layerState));
    }

    void ScenePlayer::loadAndPlay(
        const SceneLayer *       newLayers,
        uint8_t                  newCount,
        bool                     newLoop,
        const char *             newName,
        const char *             paletteDefault,
        const Protocol::ColorRGB newBaseColors[BASE_COLORS_COUNT],
        uint32_t                 nowMs,
        float                    newSpeed
    )
    {
        stop();
        scheduler.broadcastBlack();

        lCount = (newCount > SCENE_MAX_LAYERS) ? SCENE_MAX_LAYERS : newCount;
        memcpy(layers, newLayers, lCount * sizeof(SceneLayer));
        loop = newLoop;
        speed = (newSpeed < 0.1f) ? 0.1f : (newSpeed > 10.0f) ? 10.0f : newSpeed;

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

        // Build the rooted topology this scene's panel selectors resolve against. Rebuilt
        // here so it reflects the live discovered graph on every play and resume.
        rebuildTopology();

        sendPalettesToPanels();

        playing = true;

        DEBUG_IF(DEBUG_SCENE, D_PRINTF("[SCENE] play \"%s\" layers=%u loop=%s speed=%.1f\n",
                                       name, (unsigned)lCount, loop ? "true" : "false", (double)speed));

        // Arm layer gating and fire the layers that start immediately (async included).
        armLayers(nowMs, true);
    }

    void ScenePlayer::stop()
    {
        playing = false;
        memset(layerState, 0, sizeof(layerState)); // all → WAITING (re-armed on next play)
        scheduler.broadcastStop();
    }

    void ScenePlayer::resume(uint32_t nowMs)
    {
        if (lCount == 0) return;

        loadAndPlay(layers, lCount, loop, name, defaultPalette, baseColors, nowMs, speed);
    }

    void ScenePlayer::tick(uint32_t nowMs)
    {
        if (!playing) return;

        // Advance the layers that are currently running.
        for (uint8_t i = 0; i < lCount; i++) {
            if (layerState[i] != LayerState::RUNNING) continue;

            const SceneStep& step = layers[i].steps[currentStep[i]];

            if (step.durationMs == 0) continue; // infinite last step — holds, never completes

            uint32_t elapsed = (uint32_t)(nowMs - stepStartMs[i]);
            uint32_t threshold = (speed == 1.0f) ? (uint32_t)step.durationMs
                                 : (uint32_t)((float)step.durationMs / speed);

            if (elapsed < threshold) continue;

            uint8_t nextStep = currentStep[i] + 1;

            if (nextStep < layers[i].stepCount) {
                currentStep[i] = nextStep;
                stepStartMs[i] = nowMs;
                fireStep(i, nowMs);
            } else if (isAsyncLayer(i)) {
                // Async layer loops on its own, independent of the barrier.
                currentStep[i] = 0;
                stepStartMs[i] = nowMs;
                fireStep(i, nowMs);
            } else {
                // Sequence finished — hold the last frame until the scene-cycle barrier.
                layerState[i] = LayerState::DONE;
            }
        }

        // Start any gated layers whose startAfter dependency just finished.
        promoteReadyLayers(nowMs);

        // Scene-cycle barrier: governed by the synchronous (non-async) layers only.
        // When they're all DONE, restart them together (loop) or stop. Async layers
        // free-run and keep the scene alive on their own.
        bool anySync = false;
        bool anyAsync = false;

        for (uint8_t i = 0; i < lCount; i++) {
            if (isAsyncLayer(i)) {
                anyAsync = true;
                continue;
            }

            anySync = true;

            if (layerState[i] != LayerState::DONE) return; // a sync layer is still going
        }

        if (!anySync) return; // async-only scene — runs until explicitly stopped

        if (loop) {
            armLayers(nowMs, false); // restart sync layers together; leave async free-running
        } else if (!anyAsync) {
            playing = false;         // pure sync, play-once — stop
        }

        // else: sync layers play once and hold; async layers keep the scene alive.
    }

    void ScenePlayer::armLayers(uint32_t nowMs, bool includeAsync)
    {
        for (uint8_t i = 0; i < lCount; i++) {
            if (!includeAsync && isAsyncLayer(i)) continue;

            currentStep[i] = 0;
            stepStartMs[i] = nowMs;

            if (layers[i].stepCount == 0) {
                layerState[i] = LayerState::DONE; // nothing to play
            } else if (layers[i].startAfterGroupId == 0) {
                layerState[i] = LayerState::RUNNING;
            } else {
                layerState[i] = LayerState::WAITING;
            }
        }

        // Fire the ungated layers now (gated ones wait for promoteReadyLayers).
        for (uint8_t i = 0; i < lCount; i++) {
            if (!includeAsync && isAsyncLayer(i)) continue;

            if (layerState[i] == LayerState::RUNNING) fireStep(i, nowMs);
        }
    }

    int ScenePlayer::layerIndexForGroup(uint8_t groupId) const
    {
        for (uint8_t i = 0; i < lCount; i++) {
            if (layers[i].groupId == groupId) return (int)i;
        }

        return -1;
    }

    void ScenePlayer::promoteReadyLayers(uint32_t nowMs)
    {
        for (uint8_t i = 0; i < lCount; i++) {
            if (layerState[i] != LayerState::WAITING) continue;

            int dep = layerIndexForGroup(layers[i].startAfterGroupId);

            // Missing dependency is treated as satisfied (parser validates references).
            if (dep >= 0 && layerState[dep] != LayerState::DONE) continue;

            layerState[i] = LayerState::RUNNING;
            currentStep[i] = 0;
            stepStartMs[i] = nowMs;
            fireStep(i, nowMs);
        }
    }

    void ScenePlayer::fireStep(uint8_t layerIdx, uint32_t /*nowMs*/)
    {
        const SceneLayer& layer = layers[layerIdx];
        const SceneStep& step  = layer.steps[currentStep[layerIdx]];

        DEBUG_IF(DEBUG_SCENE, D_PRINTF("[SCENE] layer=%u step=%u/%u type=%s dur=%ums grp=%u\n",
                                       (unsigned)layerIdx,
                                       (unsigned)currentStep[layerIdx] + 1,
                                       (unsigned)layer.stepCount,
                                       animTypeName(step.animType),
                                       (unsigned)step.durationMs,
                                       (unsigned)layer.groupId));

        // GAP — a timed no-op. Send nothing; the layer's panels hold their current
        // state and tick() advances past this step when its duration elapses.
        if (step.animType == ANIM_GAP) return;

        uint8_t panels[SCENE_MAX_RESOLVED_PANELS];
        uint8_t panelCount = 0;

        resolvePanels(layer, panels, SCENE_MAX_RESOLVED_PANELS, panelCount);

        if (panelCount == 0) return;

        uint16_t effectiveDurationMs = ((step.durationMs == 0) || (speed == 1.0f))
            ? step.durationMs
            : (uint16_t)min((uint32_t)65535, (uint32_t)((float)step.durationMs / speed));

        if (isRunnerType(step.animType)) {
            // Kill any panel-local animation so the runner has exclusive LED control
            scheduler.sendControlToPanels(layer.groupId, ANIM_CTRL_STOP, panels, panelCount);

            // Runners are single-colour: the lit colour is `colorTo` (aliased by the JSON
            // `color` key), consistent with SOLID/STROBE/BLINK. `colorFrom` is the start
            // colour of two-colour panel effects and is unset for runners.
            Protocol::ColorRGB color = resolveColorToRgb(step.colorTo, layerIdx);
            AnimationRunner *runner = nullptr;

            // Directionality field: per-panel sweep coordinate (params[1..3]). Two orthogonal
            // choices — mode (graph hop-distance vs planar geometry) and source (root/leaves/
            // panel:N) — plus a geometric-axis `angle` for wave/chase (ripple has no axis).
            uint8_t coord[SCENE_MAX_RESOLVED_PANELS];
            uint8_t srcKind   = step.params[RUNNER_PARAM_SRC_KIND];
            uint8_t srcArg    = step.params[RUNNER_PARAM_SRC_ARG];
            bool    reverse   = (step.params[RUNNER_PARAM_FLAGS] & RUNNER_FLAG_REVERSE) != 0;
            bool    geometric = (step.params[RUNNER_PARAM_FLAGS] & RUNNER_FLAG_GEOMETRIC) != 0;
            uint8_t maxCoord;

            // Resolution tracks the graph field's span so `width` stays comparable across modes.
            uint8_t resolution = topo.maxDepth();

            if (resolution == 0) resolution = (panelCount > 1) ? (uint8_t)(panelCount - 1) : 1;

            if (geometric && geometry.valid() && step.animType == RUN_RIPPLE) {
                // Geometric ripple: Euclidean rings expanding from the source centroid(s).
                maxCoord = computeGeometricCenterField(geometry, topo, panels, panelCount,
                                                       srcKind, srcArg, reverse, resolution, coord);
            } else if (geometric && geometry.valid()) {
                // Geometric wave/chase: project panel centroids onto an axis at `angle` (srcArg*2°).
                // `source` is N/A here — an axis sweep has no origin, only a direction.
                float angleDeg = (float)srcArg * 2.0f;

                maxCoord = computeGeometricField(geometry, panels, panelCount,
                                                 angleDeg, reverse, resolution, coord);
            } else {
                // Graph hop-distance from the source set (also the fallback when a geometric step
                // can't embed: geometry degrades to the same source over hop-distance).
                maxCoord = computeDistanceField(topo, panels, panelCount,
                                                srcKind, srcArg, reverse, coord);
            }

            uint8_t width = step.params[RUNNER_PARAM_WIDTH];

            if (step.animType == RUN_WAVE) {
                runner = new WaveRunner(layer.groupId, panels, coord, panelCount, maxCoord,
                                        effectiveDurationMs, width, color);
            } else if (step.animType == RUN_RIPPLE) {
                runner = new RippleRunner(layer.groupId, panels, coord, panelCount, maxCoord,
                                          effectiveDurationMs, width, color);
            } else if (step.animType == RUN_CHASE) {
                runner = new ChaseRunner(layer.groupId, panels, coord, panelCount, maxCoord,
                                         effectiveDurationMs, color);
            }

            if (runner) scheduler.addRunner(runner);
        } else {
            scheduler.playOnPanels(layer.groupId, step.animType, step.flags,
                                   effectiveDurationMs,
                                   step.colorFrom, step.colorTo,
                                   step.params[0], step.params[1],
                                   panels, panelCount);
        }
    }

    void ScenePlayer::resolvePanels(const SceneLayer& layer, uint8_t *out, uint8_t maxLen, uint8_t& count) const
    {
        count = 0;

        PanelSet set;

        // A malformed selector resolves to nothing — the layer is simply skipped.
        if (!resolveSelector(layer.target, topo, set, tagResolver)) return;

        // Emitted in slot (discovery) order, preserving the pre-existing runner sweep order.
        emitPanelIndices(set, topo, out, maxLen, count);
    }

    void ScenePlayer::rebuildTopology()
    {
        List<Panel *> *panels = initializer.getPanels();
        uint16_t total = panels ? panels->getSize() : 0;

        if (total == 0 || total > LIGHTNET_MAX_PANELS) {
            topo.build(nullptr, 0, nullptr, 0, logicalRoot); // empty / unusable topology
            geometry.build(nullptr, 0, nullptr, nullptr, 0, 0);

            return;
        }

        uint8_t indices[LIGHTNET_MAX_PANELS];
        uint8_t edgeCounts[LIGHTNET_MAX_PANELS]; // polygon side count per panel (for geometry)
        TopoLink links[LIGHTNET_MAX_PANELS];
        uint8_t linkCount = 0;

        for (uint16_t i = 0; i < total; i++) {
            Panel *panel = panels->get(i);

            indices[i]   = (uint8_t)panel->index;

            List<Edge *> *edges = panel->edges;
            uint16_t ec    = edges ? edges->getSize() : 0;

            edgeCounts[i] = (uint8_t)ec;

            for (uint16_t e = 0; e < ec; e++) {
                Edge *edge = edges->get(e);

                if (!edge || !edge->connectedEdge || !edge->connectedEdge->panel) continue;

                uint8_t a = (uint8_t)panel->index;
                uint8_t b = (uint8_t)edge->connectedEdge->panel->index;

                // connectedEdge is normally set on the parent side only, so each link is
                // seen once — but dedupe defensively in case both sides are ever linked.
                bool seen = false;

                for (uint8_t k = 0; k < linkCount; k++) {
                    if ((links[k].panelA == a && links[k].panelB == b) ||
                        (links[k].panelA == b && links[k].panelB == a)) {
                        seen = true;
                        break;
                    }
                }

                if (seen || linkCount >= LIGHTNET_MAX_PANELS) continue;

                links[linkCount].panelA = a;
                links[linkCount].edgeA  = edge->index;
                links[linkCount].panelB = b;
                links[linkCount].edgeB  = edge->connectedEdge->index;
                linkCount++;
            }
        }

        topo.build(indices, (uint8_t)total, links, linkCount, logicalRoot);

        // Geometric directionality uses the same links but is anchored at the lowest panel
        // index (0 → lowest), matching the mobile visualizer frame regardless of logicalRoot.
        geometry.build(indices, (uint8_t)total, edgeCounts, links, linkCount, 0);
    }

    void ScenePlayer::setLogicalRoot(uint8_t panelIndex, uint32_t nowMs)
    {
        logicalRoot = panelIndex ? panelIndex : 1; // 0 → reset to physical root

        // Replaying rebuilds topo (via loadAndPlay); otherwise refresh it for the next play.
        if (playing) resume(nowMs);
        else rebuildTopology();
    }

    void ScenePlayer::sendPalettesToPanels()
    {
        scheduler.broadcastBaseColors(baseColors);

        for (uint8_t i = 0; i < lCount; i++) {
            uint8_t panels[SCENE_MAX_RESOLVED_PANELS];
            uint8_t panelCount = 0;

            resolvePanels(layers[i], panels, SCENE_MAX_RESOLVED_PANELS, panelCount);

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
                     "{\"playing\":true,\"scene\":\"%s\",\"loop\":%s,\"layers\":%u,\"speed\":%.1f}",
                     name, loop ? "true" : "false", (unsigned)lCount, (double)speed);
        }
    }
}  // namespace Lightnet
