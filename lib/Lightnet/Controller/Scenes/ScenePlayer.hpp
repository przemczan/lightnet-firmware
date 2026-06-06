#pragma once

#include <stdint.h>
#include "../../Common/AnimationTypes.hpp"
#include "../../Common/ColorRef.hpp"
#include "../../Common/LightnetConfig.hpp"
#include "../../Common/Palette.hpp"
#include "../../Common/Protocol.hpp"
#include "../Animations/AnimationScheduler.hpp"
#include "../Palettes/PaletteStore.hpp"
#include "../Panels/PanelsInitializer.hpp"
#include "../Topology/PanelSelector.hpp"

namespace Lightnet {
    // ============================================================================
    // Scene-internal capacity constants
    // ============================================================================

    static const uint8_t SCENE_MAX_LAYERS           = 8;
    static const uint8_t SCENE_MAX_STEPS            = 12;
    static const uint8_t SCENE_MAX_PANELS_PER_LAYER = 32;  // legacy authored-list cap (mirrors SEL_MAX_INDEX_LIST)
    static const uint8_t SCENE_MAX_RESOLVED_PANELS  = LIGHTNET_MAX_PANELS; // a selector can resolve to any panel
    static const uint8_t SCENE_SCHEMA_VERSION       = 2;

    // ============================================================================
    // Per-layer playback state (scene-cycle barrier model)
    // ============================================================================

    enum class LayerState : uint8_t {
        WAITING, // gated by startAfter — not yet started, panels held dark
        RUNNING, // advancing through its steps
        DONE,    // last step completed; holds until the whole-scene barrier resets
    };

    // ============================================================================
    // SceneStep — 18 bytes, generic params + ColorRef
    // ============================================================================

    struct __attribute__((__packed__)) SceneStep {
        uint8_t  animType;   // AnimationType (0-31) or RUN_* (64+)
        uint8_t  flags;      // AnimationFlags bitfield
        uint16_t durationMs; // 0 = infinite (only valid on last step of looping scene)
        ColorRef colorFrom;  // 4 bytes
        ColorRef colorTo;    // 4 bytes
        uint8_t  params[4];  // type-specific, params[0..1] sent via PREPARE
    };

    // ============================================================================
    // SceneLayer — ~268 bytes
    // ============================================================================

    struct SceneLayer {
        uint8_t       groupId;
        uint8_t       startAfterGroupId;                 // 0 = start immediately; else wait for that group's layer to finish
        uint8_t       async;                             // 1 = loop independently (ignored when startAfter is set)
        uint8_t       stepCount;
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
            ScenePlayer(
                AnimationScheduler& scheduler,
                PanelsInitializer&  initializer,
                PaletteStore&       paletteStore
            );

            // Load layers and start playing. Sends palette + base colors to panels.
            void loadAndPlay(
                const SceneLayer *       layers,
                uint8_t                  layerCount,
                bool                     loop,
                const char *             name,
                const char *             paletteDefault,
                const Protocol::ColorRGB baseColors[BASE_COLORS_COUNT],
                uint32_t                 nowMs,
                float                    speed = 1.0f
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
                tagResolver = resolver;
            }

            // Designate the logical root panel (§4.1): rebuilds the topology view and, if a scene
            // is playing, restarts it so the new rooting takes effect immediately.
            void setLogicalRoot(uint8_t panelIndex, uint32_t nowMs);

            uint8_t getLogicalRoot() const
            {
                return logicalRoot;
            }

            // True when a scene is loaded in memory (may or may not be playing).
            bool hasScene() const
            {
                return lCount > 0;
            }

            void tick(uint32_t nowMs);

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

            // Write JSON status to caller-supplied buffer.
            void writeStatusJson(char *buf, size_t bufLen) const;

        private:
            AnimationScheduler& scheduler;
            PanelsInitializer& initializer;
            PaletteStore& paletteStore;

            // Rooted view of the discovered panel tree, rebuilt from the live graph on each
            // play (and resume). Layer selectors resolve against this. See
            // docs/design/scene-portability.md.
            TopologyIndex topo;
            uint8_t logicalRoot;             // panel index the rooted view is built from (§4.1; default 1)
            const ITagResolver *tagResolver; // device tag map for `tag:` selectors (null until wired)

            SceneLayer layers[SCENE_MAX_LAYERS];
            uint8_t currentStep[SCENE_MAX_LAYERS];
            uint32_t stepStartMs[SCENE_MAX_LAYERS];
            LayerState layerState[SCENE_MAX_LAYERS];
            uint8_t lCount;
            bool loop;
            bool playing;
            float speed;
            char name[20];
            char defaultPalette[16];
            Protocol::ColorRGB baseColors[BASE_COLORS_COUNT];

            // Pre-resolved 16-stop palette per layer (resolved at load time).
            GradientStop resolvedPalettes[SCENE_MAX_LAYERS][PALETTE_STOPS];
            uint8_t resolvedPaletteCounts[SCENE_MAX_LAYERS];

            void fireStep(uint8_t layerIdx, uint32_t nowMs);
            // Resolve a layer's selector against `topo` into up to maxLen panel indices.
            void resolvePanels(const SceneLayer& layer, uint8_t *out, uint8_t maxLen, uint8_t& count) const;
            // Rebuild `topo` from the live discovered graph, rooted at `logicalRoot`.
            void rebuildTopology();
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
                return layers[i].async && (layers[i].startAfterGroupId == 0);
            }

            void sendPalettesToPanels();
            // Resolve a ColorRef to an actual RGB using the layer's palette + scene base colors.
            // Used for runner constructors that need a concrete color.
            Protocol::ColorRGB resolveColorToRgb(const ColorRef& ref, uint8_t layerIdx) const;
    };
}  // namespace Lightnet
