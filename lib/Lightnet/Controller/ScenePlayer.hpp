#pragma once

#include <stdint.h>
#include "../Common/AnimationTypes.hpp"
#include "../Common/ColorRef.hpp"
#include "../Common/LightnetConfig.hpp"
#include "../Common/Palette.hpp"
#include "../Common/Protocol.hpp"
#include "AnimationScheduler.hpp"
#include "PaletteStore.hpp"
#include "PanelsInitializer.hpp"

namespace Lightnet {

// ============================================================================
// Scene-internal capacity constants
// ============================================================================

static const uint8_t SCENE_MAX_LAYERS           = 8;
static const uint8_t SCENE_MAX_STEPS            = 12;
static const uint8_t SCENE_MAX_PANELS_PER_LAYER = 32;
static const uint8_t SCENE_SCHEMA_VERSION       = 1;

// ============================================================================
// Panel targeting mode for a layer
// ============================================================================

enum class PanelTargetMode : uint8_t {
    ALL,     // all discovered panels
    LIST,    // explicit panel-index list
    EXCLUDE, // all panels except listed
};

// ============================================================================
// SceneStep — 18 bytes, generic params + ColorRef
// ============================================================================

struct __attribute__((__packed__)) SceneStep {
    uint8_t  animType;       // AnimationType (0-31) or RUN_* (64+)
    uint8_t  flags;          // AnimationFlags bitfield
    uint16_t durationMs;     // 0 = infinite (only valid on last step of looping scene)
    ColorRef colorFrom;      // 4 bytes
    ColorRef colorTo;        // 4 bytes
    uint8_t  brightnessFrom;
    uint8_t  brightnessTo;
    uint8_t  params[4];      // type-specific, params[0..1] sent via PREPARE
};

// ============================================================================
// SceneLayer — ~268 bytes
// ============================================================================

struct SceneLayer {
    uint8_t         groupId;
    uint8_t         stepCount;
    PanelTargetMode targetMode;
    uint8_t         targetCount;                           // # entries in targetList
    char            palette[16];                           // empty = use scene default
    uint8_t         targetList[SCENE_MAX_PANELS_PER_LAYER];
    SceneStep       steps[SCENE_MAX_STEPS];
};

// ============================================================================
// ScenePlayer
// ============================================================================

class ScenePlayer {
public:
    ScenePlayer(AnimationScheduler& scheduler,
                PanelsInitializer&  initializer,
                PaletteStore&       paletteStore);

    // Load layers and start playing. Sends palette + base colors to panels.
    void loadAndPlay(const SceneLayer* layers, uint8_t layerCount,
                     bool loop, const char* name,
                     const char* paletteDefault,
                     const Protocol::ColorRGB baseColors[BASE_COLORS_COUNT],
                     uint32_t nowMs);

    void stop();
    void tick(uint32_t nowMs);

    bool        isPlaying()   const { return playing; }
    const char* sceneName()   const { return playing ? name : ""; }
    bool        sceneLoop()   const { return loop; }
    uint8_t     layerCount()  const { return lCount; }

    // Write JSON status to caller-supplied buffer.
    void writeStatusJson(char* buf, size_t bufLen) const;

private:
    AnimationScheduler& scheduler;
    PanelsInitializer&  initializer;
    PaletteStore&       paletteStore;

    SceneLayer layers[SCENE_MAX_LAYERS];
    uint8_t    currentStep[SCENE_MAX_LAYERS];
    uint32_t   stepStartMs[SCENE_MAX_LAYERS];
    bool       layerActive[SCENE_MAX_LAYERS];
    uint8_t    lCount;
    bool       loop;
    bool       playing;
    char       name[20];
    char       defaultPalette[16];
    Protocol::ColorRGB baseColors[BASE_COLORS_COUNT];

    // Pre-resolved 16-stop palette per layer (resolved at load time).
    GradientStop resolvedPalettes[SCENE_MAX_LAYERS][PALETTE_STOPS];
    uint8_t      resolvedPaletteCounts[SCENE_MAX_LAYERS];

    void fireStep(uint8_t layerIdx, uint32_t nowMs);
    void resolvePanels(const SceneLayer& layer, uint8_t* out, uint8_t& count) const;
    void sendPalettesToPanels();
    // Resolve a ColorRef to an actual RGB using the layer's palette + scene base colors.
    // Used for runner constructors that need a concrete color.
    Protocol::ColorRGB resolveColorToRgb(const ColorRef& ref, uint8_t layerIdx) const;
};

}  // namespace Lightnet
