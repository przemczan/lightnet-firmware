#pragma once

#include <stdint.h>
#include "../../Core/Anim/AnimationTypes.hpp"
#include "../../Core/Anim/ColorRef.hpp"
#include "../../Core/Anim/LightnetConfig.hpp"
#include "../../Core/Anim/Palette.hpp"
#include "../../Common/Protocol.hpp"
#include "../Animations/AnimationScheduler.hpp"
#include "../Palettes/PaletteStore.hpp"
#include "../Panels/PanelsInitializer.hpp"
#include "../Topology/PanelSelector.hpp"
#include "SceneTopology.hpp"

namespace Lightnet {
    // ============================================================================
    // Scene-internal capacity constants
    // ============================================================================

    static const uint8_t SCENE_MAX_LAYERS           = 8;
    static const uint8_t SCENE_MAX_STEPS            = 12;
    static const uint8_t SCENE_MAX_PANELS_PER_LAYER = 32;  // legacy authored-list cap (mirrors SEL_MAX_INDEX_LIST)
    static const uint8_t SCENE_MAX_RESOLVED_PANELS  = LIGHTNET_MAX_PANELS; // a selector can resolve to any panel
    static const uint8_t SCENE_SCHEMA_VERSION       = 7;  // v7: BOUNCE/RAIN/SPARKLE/MATRIX runners, `waves` (rate, alias for repeatCount), and RAIN/MATRIX `speed` (duration becomes the play window)

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

    struct __attribute__((__packed__)) SceneStep {
        uint8_t  animType;   // AnimationType (0-31) or RUN_* (64+)
        uint8_t  flags;      // AnimationFlags bitfield
        uint16_t durationMs; // 0 = infinite (only valid on last step of looping scene)
        ColorRef colorFrom;  // 4 bytes
        ColorRef colorTo;    // 4 bytes
        uint8_t  params[6];  // type-specific, params[0..1] sent via PREPARE; params[4] = RUNNER_PARAM_AMOUNT, params[5] = RUNNER_PARAM_LINES
        uint16_t speedMs;    // RAIN/SPARKLE only: drop-fall / flash period (ms). 0 = derive the rate from `durationMs` (legacy); >0 makes `durationMs` the play window instead.
    };

    // ============================================================================
    // SceneLayer — ~280 bytes
    // ============================================================================

    struct SceneLayer {
        uint8_t       groupId;
        uint8_t       startAfterGroupId;                 // 0 = start immediately; else wait for that group's layer to finish
        uint8_t       async;                             // bitmask: 0x01 = loop independently, 0x02 = non-blocking free-running (scene ignores this layer)
        uint8_t       stepCount;
        uint8_t       blend;                             // ComposeMode — how this layer composites on the panel
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
                AnimationScheduler& scheduler,
                PanelsInitializer&  initializer,
                PaletteStore&       paletteStore
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
            void setTagResolver(const ITagResolver *resolver)
            {
                sceneTopo.setTagResolver(resolver);
            }

            // Designate the logical root panel (§4.1): rebuilds the topology view and, if a scene
            // is playing, restarts it so the new rooting takes effect immediately.
            void setLogicalRoot(uint8_t panelIndex, uint32_t nowMs);

            uint8_t getLogicalRoot() const
            {
                return sceneTopo.logicalRoot();
            }

            // True when a scene is loaded in memory (may or may not be playing).
            bool hasScene() const
            {
                return lCount > 0;
            }

            void tick(uint32_t nowMs);

            // Re-resolve palettes for layers that use the scene default (no per-layer override),
            // using the new palette/colors. Pass nullptr for either argument to leave it unchanged.
            // Called when the active appearance palette or base colors change while a scene is playing.
            void reresolvePalettes(const char *newPal, const Protocol::ColorRGB *newColors);

            // Change playback speed of the current scene. Takes effect on next step.
            void setSpeed(float s)
            {
                if (s < 0.1f) s = 0.1f;

                if (s > 10.0f) s = 10.0f;

                speed = s;
            }

            float getSpeed() const
            {
                return speed;
            }

            // Numeric groupId for the layer with the given string name, or 0 if not found.
            uint8_t groupIdForName(const char *name) const;

            bool        isPlaying()   const
            {
                return playing;
            }

            const char * sceneName()   const
            {
                return playing ? name : "";
            }

            bool        sceneLoop()   const
            {
                return loop;
            }

            uint8_t     layerCount()  const
            {
                return lCount;
            }

        private:
            AnimationScheduler& scheduler;
            PaletteStore& paletteStore;

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
            // RUN_RAIN / RUN_SPARKLE: emit drops due this tick (rate-gated). Called from tick()
            // while the spawner step is the layer's current RUNNING step.
            void serviceSpawner(uint8_t layerIdx, uint32_t nowMs);
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
            // Promote WAITING layers whose dependency is DONE to RUNNING and fire them.
            void promoteReadyLayers(uint32_t nowMs);
            // True when the layer loops on its own, independent of the scene-cycle barrier.
            // async has no effect while startAfter gates the layer.
            bool isAsyncLayer(uint8_t i) const
            {
                return (layers[i].async & LAYER_ASYNC_LOOP) && (layers[i].startAfterGroupId == 0);
            }

            void sendPalettesToPanels();
            // Resolve a ColorRef to an actual RGB using the layer's palette + scene base colors.
            // Used for runner constructors that need a concrete color.
            Protocol::ColorRGB resolveColorToRgb(const ColorRef& ref, uint8_t layerIdx) const;
    };
}  // namespace Lightnet
