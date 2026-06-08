#pragma once

#include <stdint.h>
#include "Protocol.hpp"
#include "ColorRef.hpp"

namespace Lightnet {
    // ============================================================================
    // Animation Types (panel-local and references)
    // ============================================================================

    enum AnimationType : uint8_t {
        ANIM_SOLID       = 0,// hold colorTo
        ANIM_FADE        = 1,// linear color lerp from colorFrom to colorTo
        ANIM_TRANSITION  = 2,// linear color lerp from colorFrom to colorTo
        ANIM_BREATHE     = 3,// oscillate between colorFrom and colorTo (sinusoidal, parabolic approx)
        ANIM_PULSE       = 4,// fast rise → hold → fall between colorFrom and colorTo
        ANIM_BLINK       = 5,// binary on/off at period
        ANIM_HUE_CYCLE   = 6,// rotate through hues
        ANIM_STROBE      = 7,// binary flash at frequency
        ANIM_REACTIVE    = 8,// trigger-based decay (music beats)
        ANIM_GAP         = 9,// controller-only: timed no-op delay. Never sent to a panel —
                             // ScenePlayer holds the layer's panels for the step duration.

        // Modifier animations (10..31): a layer that transforms the colour accumulated
        // below it instead of producing its own. param1/param2 carry the animated scalar
        // (from→to, 0-255); colorFrom/colorTo are unused. See ColorCompose.hpp.
        ANIM_MOD_BRIGHTNESS = 10,// scale brightness (RGB multiply); identity at 255
        ANIM_MOD_SATURATION = 11,// scale saturation (HSV); identity at 255
        ANIM_MOD_HUE_SHIFT  = 12,// rotate hue (HSV); identity at 0, sweep 0-255 = full turn
        ANIM_MOD_INVERT     = 13,// blend toward RGB-inverted (255-r,255-g,255-b); identity at 0, full invert at 255

        // Controller-side runner animations (64+). Dispatched by ScenePlayer/AnimationServer
        // to AnimationScheduler::addRunner(), or compiled to per-panel PREPARE. Not a panel type.
        RUN_WAVE         = 64,
        RUN_RIPPLE       = 65,
        RUN_CHASE        = 66,
        RUN_WHEEL        = 67,
    };

    inline bool isRunnerType(uint8_t t)
    {
        return t >= 64;
    }

    // Modifier layers transform the composited accumulator rather than emitting a colour.
    inline bool isModifierType(uint8_t t)
    {
        return (t >= ANIM_MOD_BRIGHTNESS) && (t <= ANIM_MOD_INVERT);
    }

    // ============================================================================
    // Layer compositing
    // ============================================================================

    // Max concurrent composited layers (slots) a panel runs at once. The scene
    // validator rejects scenes where more than this many layers target one panel.
    static const uint8_t MAX_ANIM_SLOTS = 4;

    // Blend modes for SOURCE layers. Values must match ColorCompose.hpp ComposeOp.
    enum ComposeMode : uint8_t {
        COMPOSE_OPAQUE   = 0,// opaque top-wins (default — reproduces legacy last-write)
        COMPOSE_ADD      = 1,
        COMPOSE_MAX      = 2,
        COMPOSE_MULTIPLY = 3,
        COMPOSE_SCREEN   = 4,
        COMPOSE_DARKEN     = 6,
        COMPOSE_OVERLAY    = 7,
        COMPOSE_DIFFERENCE = 8,
        COMPOSE_SUBTRACT   = 9,
    };

    // ============================================================================
    // Animation Flags (bitfield)
    // ============================================================================

    enum AnimationFlags : uint8_t {
        FLAG_LOOP       = 0x01,// loop animation indefinitely
        FLAG_PINGPONG   = 0x02,// reverse direction at end instead of loop
        FLAG_EASING     = 0x04,// apply easing function (future)

        // "Ignore packet field, use current LED state instead."
        // Resolved on the panel at START time — no extra I2C round-trip needed.
        FLAG_CURRENT_COLOR_FROM = 0x08,// override colorFrom with current LED color
        FLAG_CURRENT_COLOR_TO   = 0x10,// override colorTo with current LED color
    };

    // ============================================================================
    // Animation Control Commands
    // ============================================================================

    enum AnimationControl : uint8_t {
        ANIM_CTRL_STOP        = 1,// stop immediately, hold current frame
        ANIM_CTRL_PAUSE       = 2,// pause (freeze at current elapsed)
        ANIM_CTRL_RESUME      = 3,// resume from paused state
        ANIM_CTRL_CLEAR_QUEUE = 4, // clear queued animations
    };

    // ============================================================================
    // Animation Parameter Update Types (for UPDATE_PARAMS packet)
    // ============================================================================

    enum AnimationParamType : uint8_t {
        PARAM_TRIGGER         = 1,// REACTIVE: trigger peak (value = peak level)
        PARAM_BRIGHTNESS_MULT = 2, // global brightness multiplier (0-255)
        PARAM_SPEED_SCALE     = 3,// speed multiplier for running animations
    };

    // ============================================================================
    // Panel-Side Animation State (22 bytes)
    // ============================================================================

    struct __attribute__((__packed__)) AnimationState {
        uint8_t  animType;   // AnimationType enum
        uint8_t  group_id;   // which group this animation belongs to (1-254; 0=reserved)
        uint8_t  flags;      // AnimationFlags bitfield
        uint8_t  transitionMs; // crossfade duration into this animation (0-255ms)
        uint16_t durationMs; // animation duration (0=infinite for looping)
        uint16_t startMs;    // millis() snapshot at start
        ColorRef colorFrom;  // 4 B — resolved at frame time against panel's palette/base colors
        ColorRef colorTo;    // 4 B
        uint8_t  param1;     // type-specific: blink period, decay rate, hue speed, etc.
        uint8_t  param2;     // type-specific
        uint8_t  composeMode;  // ComposeMode — how this SOURCE layer blends (unused for modifiers)
        uint8_t  composeOrder; // layer array index → deterministic stacking order
        uint16_t startDelayMs; // per-panel onset offset (runner sweep phase / staggering)
    };

    // ============================================================================
    // Verification (note: packet structs are defined in Protocol.hpp)
    // ============================================================================

    // static_assert(sizeof(AnimationState) == 26, "AnimationState must be 26 bytes");
}  // namespace Lightnet
