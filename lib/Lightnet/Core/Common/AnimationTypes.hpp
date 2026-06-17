#pragma once

#include <stdint.h>
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

        // Controller-side runner animations (64+). Dispatched by ScenePlayer/AnimationServer
        // to AnimationScheduler::addRunner(), or compiled to per-panel PREPARE. Not a panel type.
        RUN_WAVE         = 64,
        RUN_RIPPLE       = 65,
        RUN_CHASE        = 66,
        RUN_WHEEL        = 67,
        RUN_BOUNCE       = 68,
        RUN_RAIN         = 69,
        RUN_SPARKLE      = 70,
        RUN_MATRIX       = 71,
    };

    inline bool isRunnerType(uint8_t t)
    {
        return t >= 64;
    }

    // What an animation modulates. COLOR (default) lerps colorFrom/colorTo and contributes
    // as a SOURCE compositing layer. The others lerp valueFrom/valueTo (0-255, stored in
    // colorFrom.raw[0]/colorTo.raw[0] — colorFrom/colorTo are otherwise unused) and
    // contribute as a MODIFIER layer transforming the accumulator below it. See ColorCompose.hpp.
    enum AnimateTarget : uint8_t {
        TARGET_COLOR      = 0,
        TARGET_DIM        = 1,// identity at 255 (full), suppress toward 0 (black)
        TARGET_DESATURATE = 2,// identity at 255 (full colour), suppress toward 0 (grey)
        TARGET_HUE        = 3,// identity at 0, sweep 0-255 = full hue rotation
        TARGET_INVERT     = 4,// binary: full-strength cross-fade toward RGB-inverted
        // "Boost" variants: identity at 0, push toward max (white / full saturation) at 255.
        TARGET_BRIGHTEN   = 5,
        TARGET_SATURATE   = 6,
    };

    // ============================================================================
    // Layer compositing
    // ============================================================================

    // Max concurrent composited layers (slots) a panel runs at once. The scene
    // validator rejects scenes where more than this many layers target one panel.
    static const uint8_t MAX_ANIM_SLOTS = 18;

    // Blend modes for SOURCE layers. Values must match ColorCompose.hpp ComposeOp.
    // COMPOSE_DEFAULT is scene-authoring only (absent `"blend"` in JSON); resolve via
    // resolveComposeMode() before sending composeMode on the wire to a panel.
    enum ComposeMode : uint8_t {
        COMPOSE_DEFAULT    = 0,// scene default — opaque for normal layers, max for runners
        COMPOSE_OPAQUE     = 1,// top wins
        COMPOSE_ADD        = 2,
        COMPOSE_MAX        = 3,
        COMPOSE_MULTIPLY   = 4,
        COMPOSE_SCREEN     = 5,
        COMPOSE_DARKEN     = 6,
        COMPOSE_OVERLAY    = 7,
        COMPOSE_DIFFERENCE = 8,
        COMPOSE_SUBTRACT   = 9,
    };

    // Map a parsed scene-layer blend to a wire/panel ComposeMode.
    static inline uint8_t resolveComposeMode(uint8_t blend, bool runnerDefaultMax)
    {
        if (blend == COMPOSE_DEFAULT)
            return runnerDefaultMax ? COMPOSE_MAX : COMPOSE_OPAQUE;

        return blend;
    }

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

        // Reap-on-done: a non-looping slot carrying this flag FREES itself the instant its
        // animation finishes (instead of holding its end-state forever). Used by the RAIN/
        // SPARKLE particle spawner: each drop is a one-shot pulse on a pooled group_id, and
        // reaping releases the slot/group the moment the drop ends (which is transparent —
        // faded to colorFrom=black, or a modifier back to identity), so panels never clog.
        FLAG_REAP_ON_DONE = 0x80,
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
    // Panel-Side Animation State (23 bytes)
    // ============================================================================

    struct __attribute__((__packed__)) AnimationState {
        uint8_t  animType;   // AnimationType enum
        uint8_t  group_id;   // which group this animation belongs to (1-254; 0=reserved)
        uint8_t  flags;      // AnimationFlags bitfield
        uint8_t  transitionMs; // crossfade duration into this animation (0-255ms)
        uint16_t durationMs; // animation duration (0=infinite for looping)
        uint16_t startMs;    // millis() snapshot at start
        ColorRef colorFrom;  // 4 B — resolved at frame time against panel's palette/base colors.
                             // When animates != TARGET_COLOR, colorFrom.raw[0] is valueFrom (0-255) instead.
        ColorRef colorTo;    // 4 B — when animates != TARGET_COLOR, colorTo.raw[0] is valueTo (0-255) instead.
        uint8_t  param1;     // type-specific: blink period, decay rate, hue speed, etc.
        uint8_t  param2;     // type-specific
        uint8_t  composeMode;  // ComposeMode — how this SOURCE layer blends (unused for modifiers)
        uint8_t  composeOrder; // layer array index → deterministic stacking order
        uint16_t startDelayMs; // per-panel onset offset (runner sweep phase / staggering)
        uint8_t  animates;   // AnimateTarget — what this animation modulates (default TARGET_COLOR)
    };

    // ============================================================================
    // Verification (note: packet structs are defined in Protocol.hpp)
    // ============================================================================

    // static_assert(sizeof(AnimationState) == 26, "AnimationState must be 26 bytes");
}  // namespace Lightnet
