#pragma once

#include <stdint.h>
#include "../Common/AnimationTypes.hpp"
#include "../Common/ProtocolTypes.hpp"
#include "../Common/Palette.hpp"
#include "../Common/ColorRef.hpp"
#include "ColorCompose.hpp"
#include "../Common/LightnetConfig.hpp"

namespace Lightnet {
    // AnimationPlayer — panel-side layer compositor.
    //
    // A panel drives a single RGB output. Up to MAX_ANIM_SLOTS layers (groups) run
    // concurrently, each an independent animation state machine. Every tick the player
    // folds the occupied slots in `composeOrder` order into one colour (SOURCE layers
    // blend via composeMode; MODIFIER layers transform the accumulator) and writes it once.
    //
    // Slots are keyed by group_id: PREPARE fills a slot's pending step, START activates it.
    // A finished non-loop slot HOLDS its last value (consistent with the scene "finished
    // layer holds last frame" model). The controller drives step sequencing — each step is
    // one PREPARE followed by one general-call START.
    //
    // Portable: no Arduino/FastLED, no millis(), no LED driver. Time is passed in as a
    // 16-bit ms counter (`now`, wraps ~65.5 s). The player owns the current colour; the
    // platform reads currentColor() (when takeDirty() returns true) and drives the LED.
    class AnimationPlayer
    {
        public:
            AnimationPlayer();

            // Packet handlers (use Protocol:: packet types). `now` is a 16-bit ms counter.
            void prepare(const ::Protocol::PacketAnimationPrepare *pkt);
            void start(uint8_t seq_id, uint8_t group_id, uint16_t now);
            void control(uint8_t cmd, uint8_t group_id, uint16_t now);
            void updateParams(uint8_t seq_id, uint8_t group_id, uint8_t param_type, uint8_t value, uint8_t transitionMs, uint16_t now);

            // Direct colour (PACKET_SET_COLOR). Bypasses the compositor and becomes the current
            // colour immediately — also the source for FLAG_CURRENT_COLOR_* substitution.
            void setColorDirect(const ::Protocol::ColorRGB& c);

            // Palette + base colors — replaced via PACKET_SET_PALETTE / PACKET_SET_BASE_COLORS.
            // ColorRef resolution happens at frame time, so these take effect immediately.
            void setPalette(const GradientStop *stops, uint8_t count);
            void setBaseColors(const ::Protocol::ColorRGB colors[BASE_COLORS_COUNT]);

            // Scene compositor base colour (PACKET_SET_BACKGROUND). The layer fold starts
            // from this instead of black; an idle panel (no active layers) displays it.
            void setBackground(const ::Protocol::ColorRGB& c);

            // Called every main loop iteration (internally gated at ~16ms). `now` = 16-bit ms.
            void tick(uint16_t now);

            // Current composited colour + change flag. The platform mirrors this to the LED:
            //   player.tick(now); if (player.takeDirty()) led->write(player.currentColor());
            ::Protocol::ColorRGB currentColor() const;

            bool takeDirty();

            // Status reporting
            void fillStatus(::Protocol::PacketAnimationStatus *out, uint16_t now);

            bool isAnimating() const;
            uint8_t getAnimType() const;
            uint8_t getGroupId() const;

        private:
            // ---- One composited layer ----
            struct PACK Slot {
                static constexpr uint8_t OCCUPIED    = 1 << 0;
                static constexpr uint8_t STARTED     = 1 << 1;
                static constexpr uint8_t HOLDING     = 1 << 2;
                static constexpr uint8_t PAUSED      = 1 << 3;
                static constexpr uint8_t HAS_PENDING = 1 << 4;

                uint8_t                  flags;
                uint16_t                 pausedElapsedMs;
                uint8_t                  groupId; // group this slot serves

                AnimationState           cur; // running step
                AnimationState           pending; // next step (PREPARE awaiting its START)

                // REACTIVE state (per slot)
                uint8_t                  reactiveLevel;
                uint8_t                  reactiveDecayRate;
                uint16_t                 reactiveTriggerMs;

                ::Protocol::ColorRGB     outColor; // last computed source colour (held while paused)
                uint8_t                  outValue; // last computed modifier scalar (animates != TARGET_COLOR)
            };

            Slot slots[MAX_ANIM_SLOTS];

            uint8_t lastStartSeqId;   // dedup guard for ANIMATION_START general calls
            uint8_t lastParamsSeqId;  // dedup guard for ANIMATION_UPDATE_PARAMS general calls

            uint16_t nowMs;        // current time (ms, 16-bit), set by each public entry point
            uint16_t lastTickMs;
            bool outputDirty;      // lastOutput changed since the last takeDirty()
            ::Protocol::ColorRGB lastOutput;       // current colour (what the platform drives to the LED)
            ::Protocol::ColorRGB backgroundColor;  // compositor base (scene background; default black)

            // Palette + base colors (shared across slots; resolved at frame time).
            GradientStop palette[PALETTE_STOPS];
            uint8_t paletteCount;
            ::Protocol::ColorRGB baseColors[BASE_COLORS_COUNT];

            // ---- Slot management ----
            Slot *findSlot(uint8_t group_id);
            Slot *allocSlot(uint8_t group_id);   // existing for group, else a free one, else null
            void  clearSlot(Slot& s);
            void  activatePending(Slot& s);

            // ---- Frame evaluation ----
            void composite();
            void setOutput(const ::Protocol::ColorRGB& c);  // gated: updates lastOutput + dirty only on change
            void computeSlotOutput(Slot& s, uint16_t elapsed);  // → s.outColor (TARGET_COLOR) or s.outValue (modifier)

            ::Protocol::ColorRGB resolveColorRef(const ColorRef& ref) const;
            void resolveColors(const AnimationState& a, ::Protocol::ColorRGB *outFrom, ::Protocol::ColorRGB *outTo) const;

            // valueFrom/valueTo (0-255) for non-COLOR `animates` — see ColorRef_scalar / ColorRef_scalarValue.
            // TARGET_INVERT ignores them (fixed 0->255 range; the animType envelope drives the sweep).
            uint8_t getValueFrom(const AnimationState& a) const;
            uint8_t getValueTo(const AnimationState& a) const;

            // Lerp colorFrom/colorTo (TARGET_COLOR) or valueFrom/valueTo (modifier) by
            // progress_q8 into s.outColor / s.outValue.
            void applyProgress(const AnimationState& a, uint8_t progress_q8, Slot& s) const;

            // Type-specific handlers (write s.outColor or s.outValue via applyProgress)
            void tickFade(const AnimationState& a, uint16_t elapsed, Slot& s) const;
            void tickBreathe(const AnimationState& a, uint16_t elapsed, Slot& s) const;
            void tickPulse(const AnimationState& a, uint16_t elapsed, Slot& s) const;
            void tickBlink(const AnimationState& a, uint16_t elapsed, Slot& s) const;
            void tickHueCycle(const AnimationState& a, uint16_t elapsed, ::Protocol::ColorRGB& out) const;
            void tickStrobe(const AnimationState& a, uint16_t elapsed, Slot& s) const;
            void tickReactive(Slot& s) const;

            // Utilities
            uint8_t lerp8(uint8_t a, uint8_t b, uint8_t frac_q8) const;
            void    rgbLerp(::Protocol::ColorRGB a, ::Protocol::ColorRGB b, uint8_t frac_q8, ::Protocol::ColorRGB *out) const;
    };
}  // namespace Lightnet
