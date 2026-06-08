#include "ScenePlayer.hpp"
#include "../Animations/AnimationRunner.hpp"
#include "../Animations/RunnerCompile.hpp"
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
            case 67: return "WHEEL";
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
        : scheduler(_scheduler), paletteStore(_paletteStore),
        sceneTopo(_initializer),
        lCount(0), loop(false), playing(false), speed(1.0f)
    {
        memset(name, 0, sizeof(name));
        memset(defaultPalette, 0, sizeof(defaultPalette));
        memset(baseColors, 0, sizeof(baseColors));
        background = { 0, 0, 0 };
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
        float                    newSpeed,
        Protocol::ColorRGB       newBackground
    )
    {
        stop();
        scheduler.broadcastBlack();

        // Compositor base for this scene; pushed once so panels fold their layers over it
        // (and idle/untouched panels show it). Default black reproduces pre-v6 behaviour.
        background = newBackground;
        scheduler.broadcastBackground(background);

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
        sceneTopo.rebuild();

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

        loadAndPlay(layers, lCount, loop, name, defaultPalette, baseColors, nowMs, speed, background);
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

        sceneTopo.resolvePanels(layer.target, panels, SCENE_MAX_RESOLVED_PANELS, panelCount);

        if (panelCount == 0) return;

        uint16_t effectiveDurationMs = ((step.durationMs == 0) || (speed == 1.0f))
            ? step.durationMs
            : (uint16_t)min((uint32_t)65535, (uint32_t)((float)step.durationMs / speed));

        if (isRunnerType(step.animType)) {
            // A runner is no longer streamed: it is compiled into one local PULSE per panel
            // (per-panel onset + shape from the sweep envelope), so each panel runs it
            // autonomously and it composites with other layers like any other slot. No STOP
            // is sent — that would clobber the layers below this one.
            if (effectiveDurationMs == 0) return; // an infinite sweep has no meaning

            // What the sweep modulates — `animates` (default "color"), packed into
            // RUNNER_PARAM_FLAGS. COLOR compiles to a per-panel colour PULSE exactly as
            // before; the others compile to a MOD_* sweep instead (see below the field).
            uint8_t target = runnerTargetOf(step.params[RUNNER_PARAM_FLAGS]);

            // Runners are single-colour: the lit colour is `colorTo` (aliased by the JSON
            // `color` key), consistent with SOLID/STROBE/BLINK. `colorFrom` is the start
            // colour of two-colour panel effects and is unset for runners.
            Protocol::ColorRGB color = resolveColorToRgb(step.colorTo, layerIdx);

            // Directionality field: per-panel sweep coordinate (params[1..3]). Two orthogonal
            // choices — mode (graph hop-distance vs planar geometry) and source (root/leaves/
            // panel:N) — plus a geometric-axis `angle` for wave/chase (ripple has no axis).
            uint8_t coord[SCENE_MAX_RESOLVED_PANELS];     // near edge of each panel's band
            uint8_t coordFar[SCENE_MAX_RESOLVED_PANELS];  // far edge (geometric ripple only)
            bool haveFar   = false;                       // false ⇒ point model (far == near)
            uint8_t srcKind   = step.params[RUNNER_PARAM_SRC_KIND];
            uint8_t srcArg    = step.params[RUNNER_PARAM_SRC_ARG];
            bool reverse   = (step.params[RUNNER_PARAM_FLAGS] & RUNNER_FLAG_REVERSE) != 0;
            bool geometric = (step.params[RUNNER_PARAM_FLAGS] & RUNNER_FLAG_GEOMETRIC) != 0;
            bool repeat    = (step.params[RUNNER_PARAM_FLAGS] & RUNNER_FLAG_REPEAT) != 0;
            uint8_t maxCoord;

            const TopologyIndex& topo = sceneTopo.index();
            const PanelGeometry& geometry = sceneTopo.geom();

            // WHEEL: rotating blades radiating from a centre — a polar sweep, not a linear
            // axis or radial-ring field, so it bypasses the coord/maxCoord machinery below
            // entirely. Always loops (a wheel never stops spinning) and is colour-only
            // (ignores `animates`/`amount`/`repeat` — see RunnerCompile.hpp compileWheel);
            // requires planar geometry (no graph-hop fallback for a polar bearing).
            if (step.animType == RUN_WHEEL) {
                if (!geometry.valid()) return;

                float turns[SCENE_MAX_RESOLVED_PANELS];

                if (!computeWheelField(geometry, topo, panels, panelCount, srcKind, srcArg, reverse, turns)) return;

                uint8_t lines = step.params[RUNNER_PARAM_LINES];

                if (lines == 0) lines = 1;

                if (lines > 6)  lines = 6;

                uint8_t thicknessDeg = step.params[RUNNER_PARAM_WIDTH];

                ColorRef black = ColorRef_rgb(0, 0, 0);
                ColorRef lit   = ColorRef_rgb(color.r, color.g, color.b);
                uint8_t runnerBlend = (layer.blend == COMPOSE_OPAQUE) ? COMPOSE_MAX : layer.blend;

                for (uint8_t i = 0; i < panelCount; i++) {
                    CompiledPulse cp = compileWheel(turns[i], lines, thicknessDeg, effectiveDurationMs);

                    if (!cp.lit) continue;

                    // Swapped-colour trick (compileRepeating): the loop seam coincides with
                    // this blade's peak, giving a clean departing → dark-gap → approaching cycle.
                    scheduler.sendPrepareToPanel(panels[i], layer.groupId, ANIM_PULSE, FLAG_LOOP,
                                                 cp.durationMs, lit, black,
                                                 cp.risePct, cp.fallPct,
                                                 runnerBlend, /*composeOrder=*/ layerIdx,
                                                 cp.startDelayMs);
                }

                delayMicroseconds(300);
                scheduler.sendGroupStart(layer.groupId);

                return;
            }

            // Resolution tracks the graph field's span so `width` stays comparable across modes.
            uint8_t resolution = topo.maxDepth();

            if (resolution == 0) resolution = (panelCount > 1) ? (uint8_t)(panelCount - 1) : 1;

            if (geometric && geometry.valid() && step.animType == RUN_RIPPLE) {
                // Geometric ripple: Euclidean rings expanding from the source centroid(s); each
                // panel spans a [near, far] band so the ring lights whatever surface it intersects.
                maxCoord = computeGeometricCenterField(geometry, topo, panels, panelCount,
                                                       srcKind, srcArg, reverse, resolution,
                                                       coord, coordFar);
                haveFar = true;
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

            // Compile the sweep to a per-panel PULSE: black (colorFrom) → lit colour (colorTo).
            ColorRef black = ColorRef_rgb(0, 0, 0);
            ColorRef lit   = ColorRef_rgb(color.r, color.g, color.b);

            // A runner's dark phase should be transparent over whatever is below it, so a
            // runner layered on a background/other layer reads as an accent rather than
            // clobbering it with black. Default to MAX (identical to NORMAL over a black
            // base, i.e. a standalone runner); honour an explicit non-default blend.
            uint8_t runnerBlend = (layer.blend == COMPOSE_OPAQUE) ? COMPOSE_MAX : layer.blend;

            // Non-colour targets compile to a MOD_* sweep instead of a colour PULSE: each
            // lit panel snaps to `amount` (peak) at the sweep's onset and decays to the
            // property's identity value over its lit window — passing through and releasing
            // whatever is layered below, the modifier analogue of black→lit→black. `amount`
            // is the peak set by `amount` (default 0 = no visible effect).
            uint8_t modType     = ANIM_MOD_BRIGHTNESS; // also RUNNER_TARGET_BRIGHTNESS's values;
            uint8_t modIdentity = 255;                 // RUNNER_TARGET_COLOR never reads these.

            switch (target) {
                case RUNNER_TARGET_SATURATION: modType = ANIM_MOD_SATURATION;
                    modIdentity = 255;
                    break;
                case RUNNER_TARGET_HUE:        modType = ANIM_MOD_HUE_SHIFT;
                    modIdentity = 0;
                    break;
                case RUNNER_TARGET_INVERT:     modType = ANIM_MOD_INVERT;
                    modIdentity = 0;
                    break;
                default: break;
            }

            uint8_t modPeak = step.params[RUNNER_PARAM_AMOUNT];

            // `repeat` only has meaning for a colour sweep — see the swapped-colour note on
            // compileRepeating (RunnerCompile.hpp): looping a MOD_* sweep would lerp straight
            // from `amount` back to its identity value with no rest, producing a sawtooth.
            bool repeating = repeat && (target == RUNNER_TARGET_COLOR);

            // `repeatCount` > 1 places that many evenly-spaced waves in flight at once by
            // shortening the loop period. Count 0 or 1 both mean one wave per duration.
            uint8_t repeatCount = step.params[RUNNER_PARAM_REPEAT_COUNT];
            uint16_t repeatPeriod = effectiveDurationMs;

            if (repeating && repeatCount > 1) {
                repeatPeriod = (uint16_t)((uint32_t)effectiveDurationMs / repeatCount);

                if (repeatPeriod == 0) repeatPeriod = 1;
            }

            for (uint8_t i = 0; i < panelCount; i++) {
                CompiledPulse cp;

                switch (step.animType) {
                    case RUN_WAVE:
                        cp = repeating
                            ? compileWaveRepeating((float)coord[i], maxCoord, width, repeatPeriod)
                            : compileWave((float)coord[i], maxCoord, width, effectiveDurationMs);
                        break;
                    case RUN_CHASE:
                        cp = repeating
                            ? compileChaseRepeating(coord[i], maxCoord, repeatPeriod)
                            : compileChase(coord[i], maxCoord, effectiveDurationMs);
                        break;
                    default: // RUN_RIPPLE
                        cp = repeating
                            ? compileRippleRepeating((float)coord[i], (float)(haveFar ? coordFar[i] : coord[i]),
                                                     maxCoord, width, repeatPeriod)
                            : compileRipple((float)coord[i], (float)(haveFar ? coordFar[i] : coord[i]),
                                            maxCoord, width, effectiveDurationMs);
                        break;
                }

                // Unlit panels get no slot for this group → they stay transparent in the fold.
                // (Edge case: if a prior step lit this panel and this runner leaves it unlit —
                // width 0 / out of range — the panel holds that prior step, since no PREPARE
                // arrives and START finds nothing pending. Normal runners light every targeted
                // panel each cycle, so this only bites pathological width/coord combos.)
                if (!cp.lit) continue;

                if (target == RUNNER_TARGET_COLOR) {
                    // `repeat`: swapped colour trick (compileRepeating) — the loop seam coincides
                    // with this panel's peak, giving a clean departing → dark-gap → approaching cycle.
                    const ColorRef& from = repeating ? lit   : black;
                    const ColorRef& to   = repeating ? black : lit;
                    uint8_t flags        = repeating ? FLAG_LOOP : 0;

                    scheduler.sendPrepareToPanel(panels[i], layer.groupId, ANIM_PULSE, flags,
                                                 cp.durationMs, from, to,
                                                 cp.risePct, cp.fallPct,
                                                 runnerBlend, /*composeOrder=*/ layerIdx,
                                                 cp.startDelayMs);
                } else {
                    scheduler.sendPrepareToPanel(panels[i], layer.groupId, modType, /*flags=*/ 0,
                                                 cp.durationMs, black, black,
                                                 modPeak, modIdentity,
                                                 layer.blend, /*composeOrder=*/ layerIdx,
                                                 cp.startDelayMs);
                }
            }

            delayMicroseconds(300);
            scheduler.sendGroupStart(layer.groupId);
        } else {
            scheduler.playOnPanels(layer.groupId, step.animType, step.flags,
                                   effectiveDurationMs,
                                   step.colorFrom, step.colorTo,
                                   step.params[0], step.params[1],
                                   panels, panelCount,
                                   layer.blend, /*composeOrder=*/ layerIdx);
        }
    }

    void ScenePlayer::setLogicalRoot(uint8_t panelIndex, uint32_t nowMs)
    {
        sceneTopo.setLogicalRoot(panelIndex); // 0 → reset to physical root

        // Replaying rebuilds the topology (via loadAndPlay); otherwise refresh it for the next play.
        if (playing) resume(nowMs);
        else sceneTopo.rebuild();
    }

    void ScenePlayer::sendPalettesToPanels()
    {
        scheduler.broadcastBaseColors(baseColors);

        // Count how many layers cover each panel. A panel runs at most MAX_ANIM_SLOTS
        // composited layers; beyond that the panel drops the later layers' PREPAREs
        // (deterministically, by layer array order). Warn the author when that happens.
        uint8_t cover[LIGHTNET_MAX_PANELS + 1];

        memset(cover, 0, sizeof(cover));

        for (uint8_t i = 0; i < lCount; i++) {
            uint8_t panels[SCENE_MAX_RESOLVED_PANELS];
            uint8_t panelCount = 0;

            sceneTopo.resolvePanels(layers[i].target, panels, SCENE_MAX_RESOLVED_PANELS, panelCount);

            if (panelCount == 0) continue;

            for (uint8_t j = 0; j < panelCount; j++) {
                if (panels[j] <= LIGHTNET_MAX_PANELS && cover[panels[j]] < 255) cover[panels[j]]++;
            }

            // Panels may be in an isOn=false state (e.g. after selfTest or an
            // explicit turn-off). Turn them on now so animation output is visible.
            scheduler.turnOnPanels(panels, panelCount);

            scheduler.unicastPaletteToPanels(resolvedPalettes[i], resolvedPaletteCounts[i],
                                             panels, panelCount);
        }

        for (uint8_t a = 1; a <= LIGHTNET_MAX_PANELS; a++) {
            if (cover[a] > MAX_ANIM_SLOTS) {
                DEBUG_IF(DEBUG_SCENE, D_PRINTF("[SCENE] panel %u covered by %u layers (>%u slots) — extra layers dropped\n",
                                               (unsigned)a, (unsigned)cover[a], (unsigned)MAX_ANIM_SLOTS));
            }
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

    void ScenePlayer::reresolvePalettes(const char *newPal, const Protocol::ColorRGB *newColors)
    {
        if (!playing || lCount == 0) return;

        if (newPal && newPal[0]) {
            strncpy(defaultPalette, newPal, sizeof(defaultPalette) - 1);
            defaultPalette[sizeof(defaultPalette) - 1] = '\0';
        }

        if (newColors) {
            memcpy(baseColors, newColors, sizeof(baseColors));
        }

        for (uint8_t i = 0; i < lCount; i++) {
            if (layers[i].palette[0]) continue; // layer has its own palette — skip

            if (strcmp(defaultPalette, "userColors") == 0) {
                PaletteStore::buildUserColors(baseColors, resolvedPalettes[i], resolvedPaletteCounts[i]);
            } else if (!paletteStore.resolve(defaultPalette, resolvedPalettes[i], resolvedPaletteCounts[i])) {
                PaletteStore::buildUserColors(baseColors, resolvedPalettes[i], resolvedPaletteCounts[i]);
            }
        }
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
