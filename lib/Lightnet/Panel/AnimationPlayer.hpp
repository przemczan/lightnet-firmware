#pragma once

#include <stdint.h>
#include "../Common/AnimationTypes.hpp"
#include "../Common/Protocol.hpp"
#include "../Common/Palette.hpp"
#include "../Common/ColorRef.hpp"
#include "../Common/LightnetConfig.hpp"
#ifdef SIM_MODE
    #include "../Sim/SimRGBController.hpp"
#else
    #include "RGBController.hpp"
#endif

namespace Lightnet {
    class AnimationPlayer
    {
        public:
            AnimationPlayer();

            // Set the RGBController that this player will drive
            void setRGBController(RGBController *rgb)
            {
                rgbController = rgb;
            }

            // Packet handlers (use Protocol:: packet types)
            void prepare(const ::Protocol::PacketAnimationPrepare *pkt);
            void start(uint8_t seq_id, uint8_t group_id);
            void control(uint8_t cmd);
            void updateParams(uint8_t seq_id, uint8_t group_id, uint8_t param_type, uint8_t value, uint8_t transitionMs);

            // Palette + base colors — replaced via PACKET_SET_PALETTE / PACKET_SET_BASE_COLORS.
            // ColorRef resolution happens at frame time, so these take effect immediately
            // without disturbing the running animation.
            void setPalette(const GradientStop *stops, uint8_t count);
            void setBaseColors(const ::Protocol::ColorRGB colors[BASE_COLORS_COUNT]);

            // Called every main loop iteration (internally gated at ~16ms)
            void tick();

            // Status reporting
            void fillStatus(::Protocol::PacketAnimationStatus *out);

            // Query current state
            bool isAnimating() const
            {
                return (animType != ANIM_SOLID) || (reactiveLevel > 0);
            }

            uint8_t getAnimType() const
            {
                return animType;
            }

            uint8_t getGroupId() const
            {
                return groupId;
            }

        private:
            // Current animation state
            AnimationState queue[4]; // 4-deep animation queue (88 bytes total)
            uint8_t queueHead;   // index of first queued animation
            uint8_t queueCount;  // number of animations in queue

            // Playback state
            uint8_t animType;    // current animation type (ANIM_SOLID if none)
            uint8_t groupId;     // current group ID
            uint8_t flags;       // animation flags (LOOP, PINGPONG)
            uint8_t transitionMs; // crossfade time when starting this animation
            uint16_t durationMs; // duration of current animation
            uint16_t startMs;    // millis() snapshot when animation started
            bool paused;         // is animation paused?
            uint16_t pausedElapsedMs; // elapsed time when paused
            uint8_t lastStartSeqId;  // dedup guard for PACKET_ANIMATION_START general calls
            uint8_t lastParamsSeqId; // dedup guard for PACKET_ANIMATION_UPDATE_PARAMS general calls

            // Reactive state
            uint8_t reactiveLevel; // 0-255, for REACTIVE animations (decay model)
            uint8_t reactiveDecayRate; // param1 from REACTIVE animation
            uint16_t reactiveTriggerMs; // millis() when last triggered

            // LED control
            RGBController *rgbController; // ptr to panel's LED controller

            // Frame timing
            uint16_t lastTickMs; // last time tick() was called

            // Output state (cached from last tick)
            ::Protocol::ColorRGB currentColor;
            uint8_t currentBrightness;

            // Palette + base colors (resolved into ColorRGB when animation references them).
            // Defaults: 2-stop white→white palette and white/black/black base colors,
            // so a fresh panel without any SET_PALETTE/SET_BASE_COLORS still produces
            // sensible output.
            GradientStop palette[PALETTE_STOPS];
            uint8_t paletteCount;
            ::Protocol::ColorRGB baseColors[BASE_COLORS_COUNT];

            // Resolve a ColorRef against the panel's current palette + base colors.
            ::Protocol::ColorRGB resolveColorRef(const ColorRef& ref) const;

            // Animation evaluation
            void computeFrame(uint16_t elapsed);
            void applyToLED();
            void advanceQueue();

            // Type-specific tick functions
            void tickFade(uint16_t elapsed);
            void tickTransition(uint16_t elapsed);
            void tickBreathe(uint16_t elapsed);
            void tickPulse(uint16_t elapsed);
            void tickBlink(uint16_t elapsed);
            void tickHueCycle(uint16_t elapsed);
            void tickStrobe(uint16_t elapsed);
            void tickReactive(uint16_t elapsed);

            // Utilities
            uint8_t lerp8(uint8_t a, uint8_t b, uint8_t frac_q8);
            void    rgbLerp(::Protocol::ColorRGB a, ::Protocol::ColorRGB b, uint8_t frac_q8, ::Protocol::ColorRGB *out);

            // Resolve both animation colors (start + end) into RGB for the current frame.
            // Called at the top of every tick function so palette / base color changes
            // applied mid-flight take effect on the next frame.
            void resolveCurrentColors(::Protocol::ColorRGB *outFrom, ::Protocol::ColorRGB *outTo) const;
    };
}  // namespace Lightnet
