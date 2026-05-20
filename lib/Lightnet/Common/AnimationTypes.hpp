#pragma once

#include <stdint.h>
#include "Protocol.hpp"
#include "ColorRef.hpp"

namespace Lightnet {

// ============================================================================
// Animation Types (panel-local and references)
// ============================================================================

enum AnimationType : uint8_t {
    ANIM_SOLID       = 0,  // noop — hold final color/brightness
    ANIM_FADE        = 1,  // linear brightness interpolation
    ANIM_TRANSITION  = 2,  // linear color + brightness interpolation
    ANIM_BREATHE     = 3,  // sinusoidal brightness (parabolic approx)
    ANIM_PULSE       = 4,  // fast rise → hold → fall
    ANIM_BLINK       = 5,  // binary on/off at period
    ANIM_HUE_CYCLE   = 6,  // rotate through hues
    ANIM_STROBE      = 7,  // binary flash at frequency
    ANIM_REACTIVE    = 8,  // trigger-based decay (music beats)
};

// ============================================================================
// Animation Flags (bitfield)
// ============================================================================

enum AnimationFlags : uint8_t {
    FLAG_LOOP       = 0x01,  // loop animation indefinitely
    FLAG_PINGPONG   = 0x02,  // reverse direction at end instead of loop
    FLAG_EASING     = 0x04,  // apply easing function (future)

    // "Ignore packet field, use current LED state instead."
    // Resolved on the panel at START time — no extra I2C round-trip needed.
    FLAG_CURRENT_COLOR_FROM      = 0x08,  // override colorFrom with current LED color
    FLAG_CURRENT_COLOR_TO        = 0x10,  // override colorTo with current LED color
    FLAG_CURRENT_BRIGHTNESS_FROM = 0x20,  // override brightnessFrom with current brightness
    FLAG_CURRENT_BRIGHTNESS_TO   = 0x40,  // override brightnessTo with current brightness
};

// ============================================================================
// Animation Control Commands
// ============================================================================

enum AnimationControl : uint8_t {
    ANIM_CTRL_STOP        = 1,  // stop immediately, hold current frame
    ANIM_CTRL_PAUSE       = 2,  // pause (freeze at current elapsed)
    ANIM_CTRL_RESUME      = 3,  // resume from paused state
    ANIM_CTRL_CLEAR_QUEUE = 4,  // clear queued animations
};

// ============================================================================
// Animation Parameter Update Types (for UPDATE_PARAMS packet)
// ============================================================================

enum AnimationParamType : uint8_t {
    PARAM_TRIGGER         = 1,  // REACTIVE: trigger peak (value = peak level)
    PARAM_BRIGHTNESS_MULT = 2,  // global brightness multiplier (0-255)
    PARAM_SPEED_SCALE     = 3,  // speed multiplier for running animations
};

// ============================================================================
// Panel-Side Animation State (22 bytes)
// ============================================================================

struct __attribute__((__packed__)) AnimationState {
    uint8_t  animType;       // AnimationType enum
    uint8_t  group_id;       // which group this animation belongs to (1-254; 0=reserved)
    uint8_t  flags;          // AnimationFlags bitfield
    uint8_t  transitionMs;   // crossfade duration into this animation (0-255ms)
    uint16_t durationMs;     // animation duration (0=infinite for looping)
    uint16_t startMs;        // millis() snapshot at start
    ColorRef colorFrom;      // 4 B — resolved at frame time against panel's palette/base colors
    ColorRef colorTo;        // 4 B
    uint8_t  brightnessFrom; // start brightness
    uint8_t  brightnessTo;   // end brightness
    uint8_t  param1;         // type-specific: blink period, decay rate, hue speed, etc.
    uint8_t  param2;         // type-specific
};

// ============================================================================
// Verification (note: packet structs are defined in Protocol.hpp)
// ============================================================================

// static_assert(sizeof(AnimationState) == 22, "AnimationState must be 22 bytes");

}  // namespace Lightnet
