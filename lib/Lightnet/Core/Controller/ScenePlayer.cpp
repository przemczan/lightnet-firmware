#include "ScenePlayer.hpp"
#include "AnimationRunner.hpp"
#include "RunnerCompile.hpp"
#include "RunnerSpawn.hpp"
#include "PanelField.hpp"
#include "PanelGeometry.hpp"
#include "../../Utils/Debug.hpp"
#include <string.h>

namespace {
    // Portable min — avoids Arduino's min() macro (which would corrupt std::min) so this
    // TU compiles host-side as well as on the device.
    template<typename T>
    static inline T minT(T a, T b)
    {
        return (a < b) ? a : b;
    }

    // RAIN/SPARKLE spawner tuning (controller-side; safe to adjust).
    constexpr uint8_t SPAWN_MAX_BURST          = 8;    // max drops emitted per tick (anti-spiral)
    constexpr uint8_t SPAWN_POOL_MAX           = 64;   // cap a layer's group_id pool
    constexpr uint16_t SPARKLE_FADE_MS_PER_WIDTH = 8;  // sparkle: width(0-255) → fade ms
    constexpr uint16_t RAIN_DEFAULT_FALL_MS     = 1000;// rain: fall time when `speed` is unset
    constexpr float SPAWN_DEG2RAD            = 0.017453293f;
    constexpr uint8_t MATRIX_HEAD_RISE         = 48;   // matrix: soft leading-edge onset (0-255 of the pulse)
    constexpr float MATRIX_LINE_HALFWIDTH_K  = 2.0f;    // matrix line virtual half-width = K · panel radius (antialiased)

    inline bool isSweepRunner(uint8_t animType)
    {
        return (animType == Lightnet::RUN_WAVE) || (animType == Lightnet::RUN_RIPPLE) || (animType == Lightnet::RUN_CHASE);
    }

    uint8_t maxSweepCountForLayer(const Lightnet::SceneLayer& layer)
    {
        uint8_t maxC = 1;

        for (uint8_t s = 0; s < layer.stepCount; s++) {
            if (!isSweepRunner(layer.steps[s].animType)) continue;

            uint8_t c = Lightnet::sweepSpawnCount(layer.steps[s].params[Lightnet::RUNNER_PARAM_COUNT]);

            if (c > maxC) maxC = c;
        }

        return maxC;
    }
}

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
            case 68: return "BOUNCE";
            case 69: return "RAIN";
            case 70: return "SPARKLE";
            case 71: return "MATRIX";
            default: return "?";
        }
    }

#endif

namespace Lightnet {
    ScenePlayer::ScenePlayer(
        AnimationScheduler&      _scheduler,
        IPaletteResolver&        _paletteResolver,
        const ITopologyProvider& _topoProvider
    )
        : scheduler(_scheduler), paletteResolver(_paletteResolver),
        sceneTopo(_topoProvider),
        lCount(0), loop(false), playing(false), speed(1.0f)
    {
        memset(name, 0, sizeof(name));
        memset(defaultPalette, 0, sizeof(defaultPalette));
        memset(baseColors, 0, sizeof(baseColors));
        background = { 0, 0, 0 };
        memset(currentStep, 0, sizeof(currentStep));
        memset(stepStartMs, 0, sizeof(stepStartMs));
        memset(layerState, 0, sizeof(layerState));
        memset(bouncePhase, 0, sizeof(bouncePhase));
        memset(spawnState, 0, sizeof(spawnState));
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
        allocSpawnPools(); // reserve group_id pools for RAIN/SPARKLE spawner layers
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
                Lightnet::buildUserColors(baseColors, resolvedPalettes[i], resolvedPaletteCounts[i]);
            } else if (!paletteResolver.resolve(palName, resolvedPalettes[i], resolvedPaletteCounts[i])) {
                Lightnet::buildUserColors(baseColors, resolvedPalettes[i], resolvedPaletteCounts[i]);
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
        memset(bouncePhase, 0, sizeof(bouncePhase)); // BOUNCE always restarts forward
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

            if (step.durationMs == 0) {
                // Infinite spawner steps run until explicitly stopped.
                if (step.animType == RUN_RAIN || step.animType == RUN_SPARKLE || step.animType == RUN_MATRIX
                    || step.animType == RUN_WAVE || step.animType == RUN_RIPPLE || step.animType == RUN_CHASE) {
                    serviceSpawner(i, nowMs);
                }

                continue;
            }

            uint32_t elapsed = (uint32_t)(nowMs - stepStartMs[i]);
            uint32_t threshold = (speed == 1.0f) ? (uint32_t)step.durationMs
                                 : (uint32_t)((float)step.durationMs / speed);

            if (elapsed < threshold) {
                // Service only while the step is still inside its play window. At the boundary,
                // transition first so a finished step does not spawn an extra sweep.
                if (step.animType == RUN_RAIN || step.animType == RUN_SPARKLE || step.animType == RUN_MATRIX
                    || step.animType == RUN_WAVE || step.animType == RUN_RIPPLE || step.animType == RUN_CHASE) {
                    serviceSpawner(i, nowMs);
                }

                continue;
            }

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
                // Sequence finished — release the layer's slot so its last frame stops
                // covering lower layers while we wait for the scene-cycle barrier / loop
                // restart (mirrors the GAP handling in fireStep).
                stopLayerGroup(i);
                layerState[i] = LayerState::DONE;
            }
        }

        // Start any gated layers whose startAfter dependency just finished.
        promoteReadyLayers(nowMs);

        // Scene-cycle barrier: governed by sync and blocking-async layers only.
        // Free-running (non-blocking) async layers are invisible to the scene lifecycle.
        bool anySync = false;
        bool anyBlockingAsync = false;

        for (uint8_t i = 0; i < lCount; i++) {
            if (layers[i].disabled) continue;

            if (isAsyncLayer(i)) {
                if (!(layers[i].async & LAYER_ASYNC_NON_BLOCKING)) anyBlockingAsync = true;

                continue;
            }

            anySync = true;

            if (layerState[i] != LayerState::DONE) return; // a sync layer is still going
        }

        if (!anySync) return; // async-only scene — runs until explicitly stopped

        if (loop) {
            armLayers(nowMs, false); // restart sync layers together; leave async free-running
        } else if (!anyBlockingAsync) {
            playing = false;         // pure sync (or free-async-only), play-once — stop
        }

        // else: sync layers play once and hold; blocking async layers keep the scene alive.
    }

    void ScenePlayer::armLayers(uint32_t nowMs, bool includeAsync)
    {
        for (uint8_t i = 0; i < lCount; i++) {
            if (!includeAsync && isAsyncLayer(i)) continue;

            currentStep[i] = 0;
            stepStartMs[i] = nowMs;

            if (layers[i].stepCount == 0 || layers[i].disabled) {
                layerState[i] = LayerState::DONE; // nothing to play, or disabled
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

    uint8_t ScenePlayer::groupIdForName(const char *name) const
    {
        if (!name || !name[0]) return 0;

        for (uint8_t i = 0; i < lCount; i++) {
            if (layers[i].groupName[0] && strcmp(layers[i].groupName, name) == 0)
                return layers[i].groupId;
        }

        return 0;
    }

    // True once the awaited dependency (whole sequence, or just step `stepIdx` of it)
    // has finished. A dependency that has reached DONE has by definition completed
    // every one of its steps, regardless of which one it last ran.
    bool ScenePlayer::dependencySatisfied(uint8_t depIdx, uint8_t stepIdx) const
    {
        if (layerState[depIdx] == LayerState::DONE) return true;

        if (stepIdx == SCENE_NO_STEP_INDEX) return false;

        return currentStep[depIdx] > stepIdx;
    }

    void ScenePlayer::promoteReadyLayers(uint32_t nowMs)
    {
        for (uint8_t i = 0; i < lCount; i++) {
            if (layerState[i] != LayerState::WAITING) continue;

            int dep = layerIndexForGroup(layers[i].startAfterGroupId);

            // Missing dependency is treated as satisfied (parser validates references).
            if (dep >= 0 && !dependencySatisfied((uint8_t)dep, layers[i].startAfterStepIndex)) continue;

            layerState[i] = LayerState::RUNNING;
            currentStep[i] = 0;
            stepStartMs[i] = nowMs;
            fireStep(i, nowMs);
        }
    }

    void ScenePlayer::stopLayerGroup(uint8_t layerIdx)
    {
        const SceneLayer& layer = layers[layerIdx];

        uint8_t panels[SCENE_MAX_RESOLVED_PANELS];
        uint8_t panelCount = 0;

        sceneTopo.resolvePanels(layer.target, panels, SCENE_MAX_RESOLVED_PANELS, panelCount);

        if (panelCount > 0) {
            scheduler.sendControlToPanels(layer.groupId, ANIM_CTRL_STOP, panels, panelCount);
        }
    }

    void ScenePlayer::fireStep(uint8_t layerIdx, uint32_t nowMs)
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

        // GAP — a timed no-op for this layer's own sequence, but a previous step's
        // animation slot must be released here: a finished, non-looping animation
        // (e.g. BREATHE) otherwise stays HOLDING and keeps compositing its frozen
        // end colour on top of lower layers for the whole gap, hiding them.
        // Stopping the group clears the slot so lower layers show through again.
        if (step.animType == ANIM_GAP) {
            stopLayerGroup(layerIdx);

            return;
        }

        uint8_t panels[SCENE_MAX_RESOLVED_PANELS];
        uint8_t panelCount = 0;

        sceneTopo.resolvePanels(layer.target, panels, SCENE_MAX_RESOLVED_PANELS, panelCount);

        if (panelCount == 0) return;

        uint16_t effectiveDurationMs = ((step.durationMs == 0) || (speed == 1.0f))
            ? step.durationMs
            : (uint16_t)minT((uint32_t)65535, (uint32_t)((float)step.durationMs / speed));

        if (isRunnerType(step.animType)) {
            // RAIN/SPARKLE are particle *spawners*, not compiled pulses. fireStep just
            // (re)initialises the per-layer spawner; tick()/serviceSpawner emits drops over the
            // window. Re-seed the PRNG each window (genuinely non-repeating) and reset the rate
            // accumulator — but the pool cursor PERSISTS (set at load) so a new window's drops
            // take fresh group_ids while the previous window's drops are still draining.
            if (step.animType == RUN_RAIN || step.animType == RUN_SPARKLE || step.animType == RUN_MATRIX) {
                LayerSpawnState& st = spawnState[layerIdx];

                // XOR in layerIdx so two layers with identical settings (same cursor=0,
                // same nowMs at scene start) don't derive the same PRNG stream and end up
                // spawning identical drops in lockstep.
                st.rng           = (nowMs * 2654435761u) ^ 0x9E3779B9u ^ ((uint32_t)st.cursor << 16)
                                   ^ ((uint32_t)layerIdx * 0x01000193u) ^ 1u;
                st.accumMs       = 0;
                st.lastServiceMs = nowMs;

                return;
            }

            // Other runners are compiled into one local PULSE per panel (per-panel onset + shape
            // from the sweep envelope), so each panel runs it autonomously and it composites with
            // other layers like any other slot. No STOP is sent — that would clobber layers below.
            if (effectiveDurationMs == 0) return; // an infinite sweep has no meaning

            // What the sweep modulates — `animates` (default TARGET_COLOR). COLOR compiles to
            // a per-panel colour PULSE exactly as before; the others compile to the same
            // pulse shape but lerp valueFrom/valueTo (identity<->peak) instead of colorFrom/
            // colorTo (black<->lit), and tag the PREPARE with `animates` so the panel runs it
            // as a modifier layer.
            uint8_t target = step.animates;

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
            uint8_t maxCoord;

            // BOUNCE: a single band that sweeps back and forth forever — flip the effective
            // direction each time this step re-fires (once per scene cycle), so consecutive
            // cycles alternate the sweep direction (perpetual pendulum motion).
            if (step.animType == RUN_BOUNCE) {
                reverse ^= bouncePhase[layerIdx];
                bouncePhase[layerIdx] = !bouncePhase[layerIdx];
            }

            const TopologyIndex& topo = sceneTopo.index();
            const PanelGeometry& geometry = sceneTopo.geom();

            // Non-colour targets reuse the exact same compiled pulse as COLOR would, just
            // lerping valueFrom/valueTo (identity<->peak) instead of colorFrom/colorTo
            // (black<->lit). Identity table: DIM/DESATURATE settle at full (255); the
            // "boost" targets (HUE/INVERT/BRIGHTEN/SATURATE) settle at none (0).
            uint8_t identity = ((target == TARGET_DIM) || (target == TARGET_DESATURATE)) ? 255 : 0;
            uint8_t peak     = step.params[RUNNER_PARAM_AMOUNT];

            // WHEEL: rotating blades radiating from a centre — a polar sweep, not a linear
            // axis or radial-ring field, so it bypasses the coord/maxCoord machinery below
            // entirely. Always loops (a wheel never stops spinning); requires planar geometry
            // (no graph-hop fallback for a polar bearing).
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
                uint8_t runnerBlend = resolveComposeMode(layer.blend, /*runnerDefaultMax=*/ true);

                for (uint8_t i = 0; i < panelCount; i++) {
                    CompiledPulse cp = compileWheel(turns[i], lines, thicknessDeg, effectiveDurationMs);

                    if (!cp.lit) continue;

                    if (target == TARGET_COLOR) {
                        // Swapped-colour trick (compileRepeating): the loop seam coincides with
                        // this blade's peak, giving a clean departing → dark-gap → approaching cycle.
                        scheduler.sendPrepareToPanel(panels[i], layer.groupId, ANIM_PULSE, FLAG_LOOP,
                                                     cp.durationMs, lit, black,
                                                     cp.risePct, cp.fallPct,
                                                     runnerBlend, /*composeOrder=*/ layerIdx,
                                                     cp.startDelayMs);
                    } else {
                        // Modifier WHEEL: same loop seam trick, peak -> identity per blade pass.
                        ColorRef fromVal = ColorRef_rgb(peak, 0, 0);
                        ColorRef toVal   = ColorRef_rgb(identity, 0, 0);

                        scheduler.sendPrepareToPanel(panels[i],
                                                     layer.groupId,
                                                     ANIM_PULSE,
                                                     FLAG_LOOP,
                                                     cp.durationMs,
                                                     fromVal,
                                                     toVal,
                                                     cp.risePct,
                                                     cp.fallPct,
                                                     resolveComposeMode(layer.blend, /*runnerDefaultMax=*/ false),
                                                     /*composeOrder=*/ layerIdx,
                                                     cp.startDelayMs,
                                                     target);
                    }
                }

                scheduler.pace(300);
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

            // Bands narrower than 2 coordinate-rings leave no overlap between adjacent
            // panels' compiled pulses (RunnerCompile.hpp): each panel's pulse fades fully
            // to black exactly as its neighbour's rises from black, producing a visible
            // blink at every step. Clamp to 2 so wave/ripple/bounce always overlap.
            uint8_t width = step.params[RUNNER_PARAM_WIDTH];

            if (width < 2) width = 2;

            // Compile the sweep to a per-panel PULSE: black (colorFrom) → lit colour (colorTo).
            ColorRef black = ColorRef_rgb(0, 0, 0);
            ColorRef lit   = ColorRef_rgb(color.r, color.g, color.b);

            // A runner's dark phase should be transparent over whatever is below it, so a
            // runner layered on a background/other layer reads as an accent rather than
            // clobbering it with black. Default (absent `"blend"`) resolves to MAX; an
            // explicit `"opaque"` stays top-wins.
            uint8_t runnerBlend = resolveComposeMode(layer.blend, /*runnerDefaultMax=*/ true);

            if (step.animType == RUN_BOUNCE) {
                // Single band whose peak reflects at the field edges (center sweeps [0,
                // maxCoord]), one-shot per scene cycle; direction toggled above via
                // bouncePhase. Not spawner-driven — a perpetual pendulum is already a
                // continuous train of one.
                for (uint8_t i = 0; i < panelCount; i++) {
                    CompiledPulse cp = compileBounce((float)coord[i], maxCoord, width, effectiveDurationMs);

                    if (!cp.lit) continue;

                    if (target == TARGET_COLOR) {
                        scheduler.sendPrepareToPanel(panels[i], layer.groupId, ANIM_PULSE, 0,
                                                     cp.durationMs, black, lit,
                                                     cp.risePct, cp.fallPct,
                                                     runnerBlend, /*composeOrder=*/ layerIdx,
                                                     cp.startDelayMs);
                    } else {
                        ColorRef fromVal = ColorRef_rgb(identity, 0, 0);
                        ColorRef toVal   = ColorRef_rgb(peak, 0, 0);

                        scheduler.sendPrepareToPanel(panels[i],
                                                     layer.groupId,
                                                     ANIM_PULSE,
                                                     0,
                                                     cp.durationMs,
                                                     fromVal,
                                                     toVal,
                                                     cp.risePct,
                                                     cp.fallPct,
                                                     resolveComposeMode(layer.blend, /*runnerDefaultMax=*/ false),
                                                     /*composeOrder=*/ layerIdx,
                                                     cp.startDelayMs,
                                                     target);
                    }
                }

                scheduler.pace(300);
                scheduler.sendGroupStart(layer.groupId);

                return;
            }

            // WAVE/RIPPLE/CHASE: spawner-driven (serviceSpawner). Cache the field + step
            // params here; no PREPARE is sent from fireStep — serviceSpawner fires every
            // sweep, including the first one, on the schedule derived from `count`.
            LayerSpawnState& sst = spawnState[layerIdx];

            sst.sweepPanelCount = panelCount;
            memcpy(sst.sweepPanels, panels, panelCount);
            memcpy(sst.sweepCoord, coord, panelCount);

            if (haveFar) memcpy(sst.sweepCoordFar, coordFar, panelCount);

            sst.sweepHaveFar      = haveFar;
            sst.sweepMaxCoord     = maxCoord;
            sst.sweepWidth        = width;
            sst.sweepDurationMs   = effectiveDurationMs;
            sst.sweepSpawnIndex   = 0;
            sst.nextSpawnMs       = stepStartMs[layerIdx];

            // Emit the first sweep immediately (tick() also services the schedule, but
            // fireStep can run without a preceding service pass — e.g. loadAndPlay).
            serviceSweepSpawner(layerIdx, nowMs);
        } else if (step.animates == TARGET_COLOR) {
            scheduler.playOnPanels(layer.groupId, step.animType, step.flags,
                                   effectiveDurationMs,
                                   step.colorFrom, step.colorTo,
                                   step.params[STEP_PARAM_PREPARE_1], step.params[STEP_PARAM_PREPARE_2],
                                   panels, panelCount,
                                   resolveComposeMode(layer.blend, /*runnerDefaultMax=*/ false), /*composeOrder=*/ layerIdx);
        } else {
            // Non-colour `animates`: valueFrom/valueTo travel as ColorRef scalars on the wire.
            ColorRef from = ColorRef_scalar(step.valueFrom);
            ColorRef to   = ColorRef_scalar(step.valueTo);

            scheduler.playOnPanels(layer.groupId, step.animType, step.flags,
                                   effectiveDurationMs,
                                   from, to,
                                   step.params[STEP_PARAM_PREPARE_1], step.params[STEP_PARAM_PREPARE_2],
                                   panels, panelCount,
                                   resolveComposeMode(layer.blend, /*runnerDefaultMax=*/ false), /*composeOrder=*/ layerIdx,
                                   step.animates);
        }
    }

    // ========================================================================
    // RAIN / SPARKLE particle spawner
    // ========================================================================

    namespace {
        // Per-step drop appearance, resolved once per service call (shared by every drop and
        // every panel of a rain path). Mirrors fireStep's animates/colour/blend resolution.
        struct DropStyle {
            uint8_t            target;      // AnimateTarget
            uint8_t            identity;    // non-colour target: value the modifier decays back to
            uint8_t            peak;        // non-colour target: peak modifier amount
            uint8_t            blend;       // compose mode for this layer's drops
            Protocol::ColorRGB fromRgb;     // colour the drop fades to (default black)
            Protocol::ColorRGB litRgb;      // head colour the drop peaks at
        };

        // Scale an 8-bit channel by a 0-255 brightness factor (used for antialiasing).
        inline uint8_t scale8(uint8_t v, uint8_t b)
        {
            return (uint8_t)(((uint16_t)v * (uint16_t)b) / 255u);
        }

        // Geometric "fall coordinate" of a slot: its centroid projected onto the fall axis, signed
        // by `dir` (+1 normal, -1 for `reverse`). Drops fall from low → high fall-coordinate.
        // False if the slot has no usable centroid.
        bool fallCoordOf(
            const PanelGeometry& geo,
            const TopologyIndex& topo,
            uint8_t              slot,
            float                ca,
            float                sa,
            float                dir,
            float&               out
        )
        {
            float x, y;

            if (!geo.centroidOf(topo.panelAt(slot), x, y)) return false;

            out = (x * ca + y * sa) * dir;

            return true;
        }

        // Perpendicular coordinate (across the fall axis) — MATRIX uses it to keep a column
        // straight by preferring the descending neighbour with the least perpendicular drift.
        bool perpCoordOf(
            const PanelGeometry& geo,
            const TopologyIndex& topo,
            uint8_t              slot,
            float                ca,
            float                sa,
            float&               out
        )
        {
            float x, y;

            if (!geo.centroidOf(topo.panelAt(slot), x, y)) return false;

            out = -x * sa + y * ca;

            return true;
        }

        // Emit one drop pulse on one panel (PREPARE only; the caller fires the group START once
        // the whole drop is prepared). FLAG_REAP_ON_DONE frees the slot the instant it finishes.
        // `extraDelayMs` is the per-drop random spawn jitter — added to the pulse's onset so a drop
        // PREPAREd on the even spawn beat actually *appears* at a random time within the interval.
        // `bright` (0-255) scales the drop's peak — used for MATRIX's antialiased line edges.
        void sendDropPrepare(
            AnimationScheduler& sch,
            uint8_t             panel,
            uint8_t             group,
            uint8_t             composeOrder,
            const DropStyle&    s,
            const DropPulse&    dp,
            uint16_t            extraDelayMs,
            uint8_t             bright = 255
        )
        {
            uint32_t sd32 = (uint32_t)dp.startDelayMs + extraDelayMs;
            uint16_t sd   = (sd32 > 65535u) ? 65535u : (uint16_t)sd32;

            if (s.target == TARGET_COLOR) {
                // Instant onset (rise ≈ 0) to the head colour, fading to `from` (default black).
                ColorRef from = ColorRef_rgb(scale8(s.fromRgb.r, bright), scale8(s.fromRgb.g, bright), scale8(s.fromRgb.b, bright));
                ColorRef lit  = ColorRef_rgb(scale8(s.litRgb.r, bright), scale8(s.litRgb.g, bright), scale8(s.litRgb.b, bright));

                sch.sendPrepareToPanel(panel, group, ANIM_PULSE, FLAG_REAP_ON_DONE,
                                       dp.durationMs, from, lit, dp.risePct, dp.fallPct,
                                       s.blend, composeOrder, sd);
            } else {
                // Modifier drop: same pulse shape, identity -> peak over the lit window, then reap.
                ColorRef from = ColorRef_rgb(s.identity, 0, 0);
                ColorRef lit  = ColorRef_rgb(scale8(s.peak, bright), 0, 0);

                sch.sendPrepareToPanel(panel, group, ANIM_PULSE, FLAG_REAP_ON_DONE,
                                       dp.durationMs, from, lit, dp.risePct, dp.fallPct,
                                       s.blend, composeOrder, sd, s.target);
            }
        }
    }

    bool ScenePlayer::layerIsSpawner(uint8_t layerIdx) const
    {
        const SceneLayer& layer = layers[layerIdx];

        for (uint8_t s = 0; s < layer.stepCount; s++) {
            uint8_t t = layer.steps[s].animType;

            if (t == RUN_RAIN || t == RUN_SPARKLE || t == RUN_MATRIX
                || t == RUN_WAVE || t == RUN_RIPPLE || t == RUN_CHASE) return true;
        }

        return false;
    }

    namespace {
        // WAVE/RIPPLE/CHASE spawner pools need up to count+1 group_ids (the +1 covers a
        // sweep still draining while the next launches) — capping their pool size leaves
        // more of the shared region for RAIN/SPARKLE/MATRIX layers.
        bool layerIsSweepSpawner(const SceneLayer& layer)
        {
            for (uint8_t s = 0; s < layer.stepCount; s++) {
                if (isSweepRunner(layer.steps[s].animType)) return true;
            }

            return false;
        }
    }

    void ScenePlayer::allocSpawnPools()
    {
        memset(spawnState, 0, sizeof(spawnState));

        uint8_t maxUsed  = 0;
        uint8_t spawners = 0;

        for (uint8_t i = 0; i < lCount; i++) {
            if (layers[i].groupId > maxUsed) maxUsed = layers[i].groupId;

            if (layerIsSpawner(i)) spawners++;
        }

        if (spawners == 0) return;

        // The pool region sits ABOVE every normal layer's group_id, so drop group_ids never
        // collide with — nor, via the broadcast START, disturb — a normal layer's panel slots.
        uint16_t regionStart = (uint16_t)maxUsed + 1;
        uint16_t avail       = (254 >= regionStart) ? (uint16_t)(254 - regionStart + 1) : 0;
        uint8_t per         = (uint8_t)minT((uint16_t)SPAWN_POOL_MAX, (uint16_t)(avail / spawners));

        uint8_t k = 0;

        for (uint8_t i = 0; i < lCount; i++) {
            if (!layerIsSpawner(i)) continue;

            uint8_t size = layerIsSweepSpawner(layers[i])
                ? minT((uint8_t)(maxSweepCountForLayer(layers[i]) + 1), per)
                : per;

            spawnState[i].poolBase = (uint8_t)(regionStart + (uint16_t)k * per);
            spawnState[i].poolSize = size;
            spawnState[i].cursor   = 0;
            k++;
        }
    }

    // WAVE/RIPPLE/CHASE: fire one-shot sweeps (compileWave/compileChase/compileRipple) on a
    // fixed schedule, each on a fresh pooled group_id with FLAG_REAP_ON_DONE so it self-reaps
    // on the panel once it finishes. The field/coords/width/duration were cached by fireStep;
    // only the spawn schedule (sweepSpawnIndex/nextSpawnMs) is tracked here.
    void ScenePlayer::serviceSweepSpawner(uint8_t layerIdx, uint32_t nowMs)
    {
        const SceneLayer& layer = layers[layerIdx];
        const SceneStep& step  = layer.steps[currentStep[layerIdx]];
        LayerSpawnState& st    = spawnState[layerIdx];

        if (st.sweepPanelCount == 0 || st.sweepDurationMs == 0) return;

        uint8_t count = sweepSpawnCount(step.params[RUNNER_PARAM_COUNT]);
        uint32_t windowStart = stepStartMs[layerIdx];

        while (st.sweepSpawnIndex < count && nowMs >= st.nextSpawnMs) {
            // Resolve appearance once per spawn (same as fireStep's runner colour/target).
            uint8_t target   = step.animates;
            uint8_t identity = ((target == TARGET_DIM) || (target == TARGET_DESATURATE)) ? 255 : 0;
            uint8_t peak     = step.params[RUNNER_PARAM_AMOUNT];

            Protocol::ColorRGB color = resolveColorToRgb(step.colorTo, layerIdx);
            ColorRef black = ColorRef_rgb(0, 0, 0);
            ColorRef lit   = ColorRef_rgb(color.r, color.g, color.b);
            uint8_t runnerBlend = resolveComposeMode(layer.blend, /*runnerDefaultMax=*/ true);

            uint8_t group = spawnPoolNext(st.cursor, st.poolBase, st.poolSize);

            for (uint8_t i = 0; i < st.sweepPanelCount; i++) {
                CompiledPulse cp;

                switch (step.animType) {
                    case RUN_WAVE:
                        cp = compileWave((float)st.sweepCoord[i], st.sweepMaxCoord, st.sweepWidth, st.sweepDurationMs);
                        break;
                    case RUN_CHASE:
                        cp = compileChase(st.sweepCoord[i], st.sweepMaxCoord, st.sweepDurationMs);
                        break;
                    default: // RUN_RIPPLE
                        cp = compileRipple((float)st.sweepCoord[i],
                                           (float)(st.sweepHaveFar ? st.sweepCoordFar[i] : st.sweepCoord[i]),
                                           st.sweepMaxCoord, st.sweepWidth, st.sweepDurationMs);
                        break;
                }

                if (!cp.lit) continue;

                if (target == TARGET_COLOR) {
                    scheduler.sendPrepareToPanel(st.sweepPanels[i], group, ANIM_PULSE, FLAG_REAP_ON_DONE,
                                                 cp.durationMs, black, lit,
                                                 cp.risePct, cp.fallPct,
                                                 runnerBlend, /*composeOrder=*/ layerIdx,
                                                 cp.startDelayMs);
                } else {
                    ColorRef fromVal = ColorRef_rgb(identity, 0, 0);
                    ColorRef toVal   = ColorRef_rgb(peak, 0, 0);

                    scheduler.sendPrepareToPanel(st.sweepPanels[i], group, ANIM_PULSE, FLAG_REAP_ON_DONE,
                                                 cp.durationMs, fromVal, toVal,
                                                 cp.risePct, cp.fallPct,
                                                 resolveComposeMode(layer.blend, /*runnerDefaultMax=*/ false), /*composeOrder=*/ layerIdx,
                                                 cp.startDelayMs, target);
                }
            }

            scheduler.pace(300);
            scheduler.sendGroupStart(group);

            st.sweepSpawnIndex++;

            if (st.sweepSpawnIndex < count) {
                st.nextSpawnMs = windowStart
                                 + spawnSweepStartMs(st.sweepDurationMs, count, st.sweepSpawnIndex);
            }
        }
    }

    void ScenePlayer::serviceSpawner(uint8_t layerIdx, uint32_t nowMs)
    {
        const SceneLayer& layer = layers[layerIdx];
        const SceneStep& step  = layer.steps[currentStep[layerIdx]];
        LayerSpawnState& st    = spawnState[layerIdx];

        if (st.poolSize == 0) return; // no pool reserved → can't spawn

        if (step.animType == RUN_WAVE || step.animType == RUN_RIPPLE || step.animType == RUN_CHASE) {
            serviceSweepSpawner(layerIdx, nowMs);

            return;
        }

        uint8_t waves = step.params[RUNNER_PARAM_WAVES]; // drops per second

        if (waves == 0) waves = 1;

        uint32_t dt      = nowMs - st.lastServiceMs;

        st.lastServiceMs = nowMs;

        uint8_t due = spawnDueCount(st.accumMs, dt, waves, SPAWN_MAX_BURST);

        if (due == 0) return;

        // Spawns fire on an even beat (every `spawnInterval` ms); scatter each drop by a random
        // offset in [0, spawnInterval) so appearances look random rather than metronomic.
        uint16_t spawnInterval = (uint16_t)(1000u / waves);

        if (spawnInterval == 0) spawnInterval = 1;

        // Resolve the drop appearance once (animates / from→to colour / blend) — same as fireStep.
        DropStyle ds;

        ds.target   = step.animates;
        ds.peak     = step.params[RUNNER_PARAM_AMOUNT];
        ds.identity = ((ds.target == TARGET_DIM) || (ds.target == TARGET_DESATURATE)) ? 255 : 0;

        Protocol::ColorRGB cTo   = resolveColorToRgb(step.colorTo, layerIdx);
        Protocol::ColorRGB cFrom = resolveColorToRgb(step.colorFrom, layerIdx);

        ds.litRgb  = cTo;
        ds.fromRgb = cFrom;
        ds.blend   = resolveComposeMode(layer.blend, /*runnerDefaultMax=*/ true);

        float spd = (speed < 0.1f) ? 0.1f : speed; // global speed scales drop time

        if (step.animType == RUN_SPARKLE) {
            // SPARKLE: each drop is one random panel from the layer target — instant flash + fade.
            uint8_t panels[SCENE_MAX_RESOLVED_PANELS];
            uint8_t pc = 0;

            sceneTopo.resolvePanels(layer.target, panels, SCENE_MAX_RESOLVED_PANELS, pc);

            if (pc == 0) return;

            uint32_t fadeMs = (uint32_t)((float)((uint32_t)step.params[RUNNER_PARAM_WIDTH] * SPARKLE_FADE_MS_PER_WIDTH) / spd);

            if (fadeMs < 1) fadeMs = 1;

            if (fadeMs > 65535u) fadeMs = 65535u;

            for (uint8_t d = 0; d < due; d++) {
                uint8_t group = spawnPoolNext(st.cursor, st.poolBase, st.poolSize);
                uint8_t panel = panels[spawnRandBelow(st.rng, pc)];
                uint16_t off   = (uint16_t)spawnRandBelow(st.rng, spawnInterval);
                DropPulse dp    = sparkleFlash((uint16_t)fadeMs);

                sendDropPrepare(scheduler, panel, group, layerIdx, ds, dp, off);
                scheduler.pace(200);
                scheduler.sendGroupStart(group);
            }

            return;
        }

        // RAIN: each drop is an ordered chain of panels (source→far) — the head cascades down it
        // and the tail fades behind. Two ways to pick the chain: TOPOLOGY (a random root→leaf tree
        // path) or GEOMETRIC (a random column of the planar layout along the `angle` axis).
        const TopologyIndex& topo = sceneTopo.index();
        uint8_t n = topo.count();

        if (n == 0) return;

        bool reverse    = (step.params[RUNNER_PARAM_FLAGS] & RUNNER_FLAG_REVERSE) != 0;
        uint8_t widthRings = step.params[RUNNER_PARAM_WIDTH];
        uint32_t fallMs     = step.speedMs ? step.speedMs : RAIN_DEFAULT_FALL_MS;

        fallMs = (uint32_t)((float)fallMs / spd);

        if (fallMs < 1) fallMs = 1;

        if (fallMs > 65535u) fallMs = 65535u;

        if (step.animType == RUN_MATRIX) {
            // MATRIX: straight, constant-speed digital-rain. Unlike RAIN (which meanders down panel
            // connections at a per-drop rate), every MATRIX drop falls at the SAME steady speed and
            // has a softened leading edge. Supports both directionality modes.
            uint8_t rise = MATRIX_HEAD_RISE;          // soft onset
            uint8_t fall = (uint8_t)(255 - rise);

            if ((step.params[RUNNER_PARAM_FLAGS] & RUNNER_FLAG_GEOMETRIC) && sceneTopo.geom().valid()) {
                // GEOMETRIC: each drop is a straight LINE down the `angle` axis at a random panel's
                // perpendicular position; it lights every panel the line *overlaps* (within that
                // panel's own radius) — it draws a line, it does not follow panel connections.
                // Onset = the panel's actual distance down the axis (constant velocity = span/fallMs).
                const PanelGeometry& geo = sceneTopo.geom();

                float th  = (float)step.params[RUNNER_PARAM_SRC_ARG] * 2.0f * SPAWN_DEG2RAD; // angle = srcArg·2°
                float ca  = cosf(th), sa = sinf(th);
                float dir = reverse ? -1.0f : 1.0f;

                // Candidate panels (have a centroid) + the global along-extent (constant-speed ref).
                uint8_t cand[LIGHTNET_MAX_PANELS];
                uint8_t candCount = 0;
                float alongMin = 0.0f, alongMax = 0.0f;

                for (uint8_t s = 0; s < n; s++) {
                    float fc;

                    if (!fallCoordOf(geo, topo, s, ca, sa, dir, fc)) continue;

                    if (candCount == 0 || fc < alongMin) alongMin = fc;

                    if (candCount == 0 || fc > alongMax) alongMax = fc;

                    cand[candCount++] = s;
                }

                if (candCount == 0) return;

                float alongSpan = (alongMax - alongMin > 0.0f) ? (alongMax - alongMin) : 1.0f;

                for (uint8_t d = 0; d < due; d++) {
                    uint8_t group  = spawnPoolNext(st.cursor, st.poolBase, st.poolSize);
                    uint16_t off    = (uint16_t)spawnRandBelow(st.rng, spawnInterval);
                    uint8_t anchor = cand[spawnRandBelow(st.rng, candCount)];

                    float anchorPerp;

                    if (!perpCoordOf(geo, topo, anchor, ca, sa, anchorPerp)) continue;

                    // Tail: constant-speed fade over `width` panel-widths of travel.
                    float panelSize = geo.circumradiusOf(topo.panelAt(anchor));

                    if (panelSize <= 0.0f) panelSize = alongSpan / (float)((n > 1) ? n : 1);

                    uint16_t tailDur = (uint16_t)((float)(widthRings ? widthRings : 1) * panelSize * (float)fallMs / alongSpan);

                    if (tailDur == 0) tailDur = 1;

                    // The line has a soft virtual half-width; a panel lights with brightness that
                    // falls off (antialiased, smoothstep) with its perpendicular distance to the
                    // line — full on the line, fading to nothing at the edge — so the column reads
                    // as a smooth falling line, not a few hard-lit panels.
                    float lineHalfWidth = panelSize * MATRIX_LINE_HALFWIDTH_K;

                    if (lineHalfWidth <= 0.0f) lineHalfWidth = 0.001f;

                    for (uint8_t c = 0; c < candCount; c++) {
                        uint8_t s = cand[c];
                        float sp, sf;

                        if (!perpCoordOf(geo, topo, s, ca, sa, sp)) continue;

                        float dd = fabsf(sp - anchorPerp);

                        if (dd >= lineHalfWidth) continue; // outside the line band

                        float t      = dd / lineHalfWidth;              // 0 = on the line, 1 = edge
                        uint8_t bright = (uint8_t)((1.0f - t * t * (3.0f - 2.0f * t)) * 255.0f); // smoothstep falloff

                        if (bright < 12) continue; // skip near-black edge panels

                        fallCoordOf(geo, topo, s, ca, sa, dir, sf);

                        uint16_t startDelay = (uint16_t)((sf - alongMin) / alongSpan * (float)fallMs);
                        DropPulse dp = { startDelay, tailDur, rise, fall };

                        sendDropPrepare(scheduler, topo.panelAt(s), group, layerIdx, ds, dp, off, bright);
                    }

                    scheduler.pace(300);
                    scheduler.sendGroupStart(group);
                }

                return;
            }

            // TOPOLOGY: a random root→leaf tree path, but at constant speed — each hop takes
            // fall-time / maxDepth (vs RAIN's per-streak rate), so all drops fall at the same rate.
            uint8_t leaves[LIGHTNET_MAX_PANELS];
            uint8_t parent[LIGHTNET_MAX_PANELS];
            uint8_t leafCount = 0;

            for (uint8_t s = 0; s < n; s++) {
                parent[s] = topo.parentOf(s);

                if (topo.isLeaf(s)) leaves[leafCount++] = s;
            }

            if (leafCount == 0) return;

            uint8_t rootSlot = topo.root();
            uint8_t maxDepth = topo.maxDepth();

            if (maxDepth == 0) maxDepth = 1;

            for (uint8_t d = 0; d < due; d++) {
                uint8_t group = spawnPoolNext(st.cursor, st.poolBase, st.poolSize);
                uint16_t off   = (uint16_t)spawnRandBelow(st.rng, spawnInterval);
                uint8_t leaf  = leaves[spawnRandBelow(st.rng, leafCount)];

                uint8_t path[LIGHTNET_MAX_PANELS];
                uint8_t len = spawnBuildPath(parent, leaf, rootSlot, path, LIGHTNET_MAX_PANELS);

                if (len == 0) continue;

                for (uint8_t i = 0; i < len; i++) {
                    uint8_t pos = reverse ? (uint8_t)(len - 1 - i) : i;
                    DropPulse dp  = rainDropAt((uint16_t)fallMs, widthRings, pos, (uint8_t)(maxDepth + 1)); // constant speed

                    dp.risePct = rise;
                    dp.fallPct = fall;

                    sendDropPrepare(scheduler, topo.panelAt(path[i]), group, layerIdx, ds, dp, off);
                }

                scheduler.pace(300);
                scheduler.sendGroupStart(group);
            }

            return;
        }

        if ((step.params[RUNNER_PARAM_FLAGS] & RUNNER_FLAG_GEOMETRIC) && sceneTopo.geom().valid()) {
            // GEOMETRIC rain: drops fall along the planar `angle` axis (the *visual* down), not the
            // tree. A drop is a **1-wide streak following actual panel connections downhill**: it
            // starts at a "top" panel (a local minimum of the fall coordinate) and repeatedly steps
            // to the neighbour that descends most, until it can't. This stays one panel wide on any
            // layout (no fuzzy width band) and naturally cascades top→bottom. `reverse` (or
            // `angle ± 180°`) flips which way is "down".
            const PanelGeometry& geo = sceneTopo.geom();

            float th  = (float)step.params[RUNNER_PARAM_SRC_ARG] * 2.0f * SPAWN_DEG2RAD; // angle = srcArg·2°
            float ca  = cosf(th), sa = sinf(th);
            float dir = reverse ? -1.0f : 1.0f;

            // Top panels = local minima of the fall coordinate (no neighbour higher up).
            uint8_t tops[LIGHTNET_MAX_PANELS];
            uint8_t topCount = 0;

            for (uint8_t s = 0; s < n; s++) {
                float fc;

                if (!fallCoordOf(geo, topo, s, ca, sa, dir, fc)) continue;

                bool isTop = true;

                for (uint8_t k = 0; k < topo.degree(s); k++) {
                    float nf;

                    if (fallCoordOf(geo, topo, topo.neighborSlot(s, k), ca, sa, dir, nf) && nf < fc) {
                        isTop = false;
                        break;
                    }
                }

                if (isTop) tops[topCount++] = s;
            }

            if (topCount == 0) return;

            for (uint8_t d = 0; d < due; d++) {
                uint8_t group = spawnPoolNext(st.cursor, st.poolBase, st.poolSize);
                uint16_t off   = (uint16_t)spawnRandBelow(st.rng, spawnInterval); // per-drop jitter

                // Build the streak: from a random top, step to the steepest-descending neighbour
                // until none descends further. Fall coordinate strictly increases → no cycles.
                uint8_t chain[LIGHTNET_MAX_PANELS];
                uint8_t len = 0;
                uint8_t cur = tops[spawnRandBelow(st.rng, topCount)];
                float curF;

                if (!fallCoordOf(geo, topo, cur, ca, sa, dir, curF)) continue;

                while (len < n) {
                    chain[len++] = cur;

                    uint8_t best = 0xFF;
                    float bestF = curF;

                    for (uint8_t k = 0; k < topo.degree(cur); k++) {
                        uint8_t v = topo.neighborSlot(cur, k);
                        float vf;

                        if (fallCoordOf(geo, topo, v, ca, sa, dir, vf) && vf > bestF) {
                            bestF = vf;
                            best  = v;
                        }
                    }

                    if (best == 0xFF) break; // reached a low point — streak ends

                    cur  = best;
                    curF = bestF;
                }

                for (uint8_t i = 0; i < len; i++) {
                    DropPulse dp = rainDropAt((uint16_t)fallMs, widthRings, i, len);

                    sendDropPrepare(scheduler, topo.panelAt(chain[i]), group, layerIdx, ds, dp, off);
                }

                scheduler.pace(300);
                scheduler.sendGroupStart(group);
            }

            return;
        }

        // TOPOLOGY rain: random root→leaf tree path (the default).
        uint8_t leaves[LIGHTNET_MAX_PANELS];
        uint8_t parent[LIGHTNET_MAX_PANELS];
        uint8_t leafCount = 0;

        for (uint8_t s = 0; s < n; s++) {
            parent[s] = topo.parentOf(s); // 0xFF for the root → matches spawnBuildPath's sentinel

            if (topo.isLeaf(s)) leaves[leafCount++] = s;
        }

        if (leafCount == 0) return;

        uint8_t rootSlot = topo.root();

        for (uint8_t d = 0; d < due; d++) {
            uint8_t group = spawnPoolNext(st.cursor, st.poolBase, st.poolSize);
            uint8_t leaf  = leaves[spawnRandBelow(st.rng, leafCount)];
            uint16_t off   = (uint16_t)spawnRandBelow(st.rng, spawnInterval); // per-drop jitter

            uint8_t path[LIGHTNET_MAX_PANELS];
            uint8_t len = spawnBuildPath(parent, leaf, rootSlot, path, LIGHTNET_MAX_PANELS);

            if (len == 0) continue;

            for (uint8_t i = 0; i < len; i++) {
                uint8_t pos   = reverse ? (uint8_t)(len - 1 - i) : i;
                uint8_t panel = topo.panelAt(path[i]);
                DropPulse dp    = rainDropAt((uint16_t)fallMs, widthRings, pos, len);

                sendDropPrepare(scheduler, panel, group, layerIdx, ds, dp, off);
            }

            scheduler.pace(300);
            scheduler.sendGroupStart(group);
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
            if (layers[i].disabled) continue;

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

    void ScenePlayer::reresolvePalettes(const char *newPal, const uint8_t *baseColorBytes)
    {
        Protocol::ColorRGB rgb[BASE_COLORS_COUNT];

        if (baseColorBytes) {
            for (uint8_t i = 0; i < BASE_COLORS_COUNT; i++) {
                rgb[i].r = baseColorBytes[i * 3 + 0];
                rgb[i].g = baseColorBytes[i * 3 + 1];
                rgb[i].b = baseColorBytes[i * 3 + 2];
            }
        }

        reresolvePalettes(newPal, baseColorBytes ? rgb : nullptr);
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
                Lightnet::buildUserColors(baseColors, resolvedPalettes[i], resolvedPaletteCounts[i]);
            } else if (!paletteResolver.resolve(defaultPalette, resolvedPalettes[i], resolvedPaletteCounts[i])) {
                Lightnet::buildUserColors(baseColors, resolvedPalettes[i], resolvedPaletteCounts[i]);
            }
        }
    }
}  // namespace Lightnet
