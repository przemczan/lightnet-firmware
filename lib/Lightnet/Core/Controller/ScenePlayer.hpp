#pragma once

#include <stdint.h>
#include "../Common/AnimationTypes.hpp"
#include "../Common/ColorRef.hpp"
#include "../Common/LightnetConfig.hpp"
#include "../Common/Palette.hpp"
#include "../Common/ProtocolTypes.hpp"
#include "../Common/UserColors.hpp"
#include "IPaletteResolver.hpp"
#include "AnimationScheduler.hpp"
#include "PanelSelector.hpp"
#include "SceneTopology.hpp"

namespace Lightnet {
    // ============================================================================
    // Scene-internal capacity constants
    // ============================================================================

    static const uint8_t SCENE_MAX_LAYERS           = 8;
    static const uint8_t SCENE_MAX_STEPS            = 12;
    static const uint8_t SCENE_MAX_PANELS_PER_LAYER = 32;  // legacy authored-list cap (mirrors SEL_MAX_INDEX_LIST)
    static const uint8_t SCENE_MAX_RESOLVED_PANELS  = LIGHTNET_MAX_PANELS; // a selector can resolve to any panel
    static const uint8_t SCENE_SCHEMA_VERSION       = 8;  // v8: step `id` + startAfter "group:stepId" (wait for a specific step, not the whole sequence)
    // v7: BOUNCE/RAIN/SPARKLE/MATRIX runners, `waves` (rate, alias for repeatCount), and RAIN/MATRIX `speed` (duration becomes the play window)

    // Sentinel for SceneLayer::startAfterStepIndex meaning "whole sequence" (no specific step targeted).
    static const uint8_t SCENE_NO_STEP_INDEX = 0xFF;

    // ============================================================================
    // Per-layer playback state (scene-cycle barrier model)
    // ============================================================================

    enum class LayerState : uint8_t {
        WAITING, // gated by startAfter — not yet started, panels held dark
        RUNNING, // advancing through its steps
        DONE,    // last step completed; holds until the whole-scene barrier resets
    };

    // ============================================================================
    // SceneStep — 22 bytes, generic params + ColorRef
    // ============================================================================

    // Indices into SceneStep::params forwarded as PacketAnimationPrepare.param1/param2.
    static const uint8_t STEP_PARAM_PREPARE_1 = 0;
    static const uint8_t STEP_PARAM_PREPARE_2 = 1;

    struct __attribute__((__packed__)) SceneStep {
        uint8_t  animType;   // AnimationType (0-31) or RUN_* (64+)
        uint8_t  flags;      // AnimationFlags bitfield
        uint16_t durationMs; // 0 = infinite (only valid on last step of looping scene)
        ColorRef colorFrom;  // 4 bytes
        ColorRef colorTo;    // 4 bytes
        uint8_t  params[6];  // type-specific; STEP_PARAM_PREPARE_* → PREPARE; runner slots → PanelField.hpp
        uint16_t speedMs;    // RAIN/SPARKLE only: drop-fall / flash period (ms). 0 = derive the rate from `durationMs` (legacy); >0 makes `durationMs` the play window instead.
        uint8_t  animates;   // AnimateTarget — what this animation modulates (default TARGET_COLOR)
        uint8_t  valueFrom;  // animates != TARGET_COLOR: scalar (0-255) ramp start ("from")
        uint8_t  valueTo;    // animates != TARGET_COLOR: scalar (0-255) ramp end ("to")
    };

    // ============================================================================
    // SceneLayer — ~280 bytes
    // ============================================================================

    struct SceneLayer {
        uint8_t       groupId;
        uint8_t       startAfterGroupId;                 // 0 = start immediately; else wait for that group's layer
        uint8_t       startAfterStepIndex;                // SCENE_NO_STEP_INDEX = wait for the whole sequence; else wait for this step (0-based) of startAfterGroupId's layer
        uint8_t       async;                             // bitmask: 0x01 = loop independently, 0x02 = non-blocking free-running (scene ignores this layer)
        uint8_t       stepCount;
        uint8_t       blend;                             // ComposeMode — COMPOSE_DEFAULT when absent from JSON
        bool          disabled;                          // true = layer is skipped entirely during playback
        char          groupName[16];                     // original string name, empty for numeric-only groups
        char          palette[16];                       // empty = use scene default
        PanelSelector target;                            // which panels this layer drives (resolved at play time)
        SceneStep     steps[SCENE_MAX_STEPS];
    };

    // ============================================================================
    // ScenePlayer
    // ============================================================================

    class ScenePlayer
    {
        public:
            // Bitmask constants for SceneLayer::async.
            static const uint8_t LAYER_ASYNC_LOOP         = 0x01; // loops independently
            static const uint8_t LAYER_ASYNC_NON_BLOCKING = 0x02; // free-running: scene ignores this layer

            ScenePlayer(
                AnimationScheduler&      scheduler,
                IPaletteResolver&        paletteResolver,
                const ITopologyProvider& topoProvider
            );

            // Load layers and start playing. Sends palette + base colors to panels.
            void loadAndPlay(
                const SceneLayer *layers,
                uint8_t layerCount,
                bool loop,
                const char *name,
                const char *paletteDefault,
                const Protocol::ColorRGB baseColors[BASE_COLORS_COUNT],
                uint32_t nowMs,
                float speed = 1.0f,
                Protocol::ColorRGB background = Protocol::ColorRGB{ 0, 0, 0 }
            );

            void stop();

            // Restart the scene that was most recently loaded, if any. Intended for
            // power-on after a suspend: stop() keeps all scene data in memory so this
            // can replay it without reloading from disk.
            // Does nothing if no scene has been loaded (lCount == 0).
            void resume(uint32_t nowMs);

            // Set the device tag map used to resolve `tag:` selectors. Null = tags resolve empty.
            void setTagResolver(const ITagResolver *resolver);

            // Designate the logical root panel (§4.1): rebuilds the topology view and, if a scene
            // is playing, restarts it so the new rooting takes effect immediately.
            void setLogicalRoot(uint8_t panelIndex, uint32_t nowMs);

            uint8_t getLogicalRoot() const;

            // True when a scene is loaded in memory (may or may not be playing).
            bool hasScene() const;

            void tick(uint32_t nowMs);

            // Re-resolve palettes for layers that use the scene default (no per-layer override),
            // using the new palette/colors. Pass nullptr for either argument to leave it unchanged.
            // Called when the active appearance palette or base colors change while a scene is playing.
            void reresolvePalettes(const char *newPal, const Protocol::ColorRGB *newColors);
            void reresolvePalettes(const char *newPal, const uint8_t *baseColorBytes);

            // Change playback speed of the current scene. Takes effect on next step.
            void setSpeed(float s);

            float getSpeed() const;

            // Numeric groupId for the layer with the given string name, or 0 if not found.
            uint8_t groupIdForName(const char *name) const;

            bool        isPlaying()   const;

            const char * sceneName()   const;

            bool        sceneLoop()   const;

            uint8_t     layerCount()  const;

        private:
            AnimationScheduler& scheduler;
            IPaletteResolver& paletteResolver;

            // Topology/targeting: owns the panel-tree views (graph/rooted index/geometry),
            // the logical root, the tag resolver, and selector resolution. Rebuilt from the
            // live discovered tree on each play/resume. See SceneTopology.hpp.
            SceneTopology sceneTopo;

            SceneLayer layers[SCENE_MAX_LAYERS];
            uint8_t currentStep[SCENE_MAX_LAYERS];
            uint32_t stepStartMs[SCENE_MAX_LAYERS];
            LayerState layerState[SCENE_MAX_LAYERS];
            // RUN_BOUNCE: sweep direction toggled each time the step re-fires, so a single
            // band sweeps back and forth across scene cycles (perpetual pendulum motion).
            // Reset to false on stop()/load — a fresh play always starts in the forward direction.
            bool bouncePhase[SCENE_MAX_LAYERS];

            // RUN_RAIN / RUN_SPARKLE particle-spawner state (see RunnerSpawn.hpp). Serviced over
            // the step window in tick(); emits one-shot drop pulses at `waves`/s on pooled
            // group_ids that self-reap on the panel (FLAG_REAP_ON_DONE). `cursor` PERSISTS across
            // a window re-fire so new drops take fresh ids while old ones drain (soft seam);
            // pool base/size are assigned once at load (allocSpawnPools).
            struct LayerSpawnState {
                uint32_t rng;           // PRNG state (re-seeded each window)
                uint32_t accumMs;       // spawn-rate accumulator
                uint32_t lastServiceMs; // for dt between services
                uint16_t cursor;        // pool round-robin cursor (persists across re-fire)
                uint8_t  poolBase;      // first group_id of this layer's drop pool
                uint8_t  poolSize;      // pool length (0 = no pool / not a spawner layer)

                // RUN_WAVE/RUN_RIPPLE/RUN_CHASE spawner state: the directionality field and
                // step params are computed once in fireStep and cached here; serviceSpawner
                // fires one-shot sweeps (compileWave/compileChase/compileRipple) on a fixed
                // schedule derived from `count` (RUNNER_PARAM_COUNT).
                uint8_t  sweepPanels[SCENE_MAX_RESOLVED_PANELS];
                uint8_t  sweepCoord[SCENE_MAX_RESOLVED_PANELS];
                uint8_t  sweepCoordFar[SCENE_MAX_RESOLVED_PANELS]; // geometric ripple only
                uint8_t  sweepPanelCount;
                uint8_t  sweepMaxCoord;
                bool     sweepHaveFar;
                uint8_t  sweepWidth;
                uint16_t sweepDurationMs; // each sweep's travel time == effectiveDurationMs of the step
                uint8_t  sweepSpawnIndex; // how many spawns fired this step window (0..count)
                uint32_t nextSpawnMs;     // absolute time of the next due sweep
            };
            LayerSpawnState spawnState[SCENE_MAX_LAYERS];

            uint8_t lCount;
            bool loop;
            bool playing;
            float speed;
            char name[20];
            char defaultPalette[16];
            Protocol::ColorRGB baseColors[BASE_COLORS_COUNT];
            Protocol::ColorRGB background; // compositor base sent to panels at play start

            // Pre-resolved 16-stop palette per layer (resolved at load time).
            GradientStop resolvedPalettes[SCENE_MAX_LAYERS][PALETTE_STOPS];
            uint8_t resolvedPaletteCounts[SCENE_MAX_LAYERS];

            void fireStep(uint8_t layerIdx, uint32_t nowMs);
            // Clears the layer's animation slot on its panels (ANIM_CTRL_STOP) so it stops
            // contributing to the composite — used when a layer hits a GAP step or finishes
            // its sequence, so a held last frame doesn't permanently cover lower layers.
            void stopLayerGroup(uint8_t layerIdx);
            // Halt WAVE/RIPPLE/CHASE pooled sweeps on this layer's panels and clear the cache.
            void stopSpawnSweeps(uint8_t layerIdx);
            // RUN_RAIN / RUN_SPARKLE / RUN_MATRIX / RUN_WAVE / RUN_RIPPLE / RUN_CHASE: emit
            // drops/sweeps due this tick (rate-gated). Called from tick() while the spawner
            // step is the layer's current RUNNING step.
            void serviceSpawner(uint8_t layerIdx, uint32_t nowMs);
            // RUN_WAVE / RUN_RIPPLE / RUN_CHASE: fire one-shot sweeps on the schedule cached
            // in spawnState by fireStep (nextSpawnMs/sweepSpawnIndex). Split out of
            // serviceSpawner for readability — the RAIN/SPARKLE/MATRIX particle model and the
            // sweep model share only the group_id pool.
            void serviceSweepSpawner(uint8_t layerIdx, uint32_t nowMs);
            // True if any step of the layer is a particle-spawner runner (RAIN/SPARKLE).
            bool layerIsSpawner(uint8_t layerIdx) const;
            // Assign each spawner layer a contiguous group_id pool above all normal layer groups.
            void allocSpawnPools();
            // Initialise per-layer state from startAfter gating and fire the ungated layers.
            // includeAsync=false leaves async layers untouched (used on the loop barrier so
            // they free-run across scene cycles); =true arms everything (initial start).
            void armLayers(uint32_t nowMs, bool includeAsync);
            // Index of the layer owning groupId, or -1 if none.
            int  layerIndexForGroup(uint8_t groupId) const;
            // True once layer `depIdx` has finished (whole sequence) or, if stepIdx !=
            // SCENE_NO_STEP_INDEX, has progressed past that step of its sequence.
            bool dependencySatisfied(uint8_t depIdx, uint8_t stepIdx) const;
            // Promote WAITING layers whose dependency is satisfied to RUNNING and fire them.
            void promoteReadyLayers(uint32_t nowMs);
            // True when the layer loops on its own, independent of the scene-cycle barrier.
            // async has no effect while startAfter gates the layer.
            bool isAsyncLayer(uint8_t i) const;

            void sendPalettesToPanels();
            // Resolve a ColorRef to an actual RGB using the layer's palette + scene base colors.
            // Used for runner constructors that need a concrete color.
            Protocol::ColorRGB resolveColorToRgb(const ColorRef& ref, uint8_t layerIdx) const;
    };
}  // namespace Lightnet
