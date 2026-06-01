#include "AnimationPlayer.hpp"
#include "../Utils/Debug.hpp"
#include "Arduino.h"

namespace Lightnet {
    AnimationPlayer::AnimationPlayer()
        : queueHead(0), queueCount(0), animType(ANIM_SOLID), groupId(0), flags(0),
        transitionMs(0), durationMs(0), startMs(0), paused(false), pausedElapsedMs(0),
        lastStartSeqId(0xFF), lastParamsSeqId(0xFF),
        reactiveLevel(0), reactiveDecayRate(0), reactiveTriggerMs(0),
        rgbController(nullptr), lastTickMs(0), paletteCount(2)
    {
        currentColor = { 0, 0, 0 };

        // Default palette: white at both ends so any palette-position lookup returns white
        // until the controller pushes a real palette.
        palette[0] = { 0, 0xFF, 0xFF, 0xFF };
        palette[1] = { 255, 0xFF, 0xFF, 0xFF };

        // Default base colors: white / black / black.
        baseColors[0] = { 0xFF, 0xFF, 0xFF };
        baseColors[1] = { 0x00, 0x00, 0x00 };
        baseColors[2] = { 0x00, 0x00, 0x00 };
    }

    void AnimationPlayer::setPalette(const GradientStop *stops, uint8_t count)
    {
        if (count == 0) return;

        if (count > PALETTE_STOPS) count = PALETTE_STOPS;

        for (uint8_t i = 0; i < count; i++) {
            palette[i] = stops[i];
        }

        paletteCount = count;
    }

    void AnimationPlayer::setBaseColors(const ::Protocol::ColorRGB colors[BASE_COLORS_COUNT])
    {
        for (uint8_t i = 0; i < BASE_COLORS_COUNT; i++) {
            baseColors[i] = colors[i];
        }
    }

    ::Protocol::ColorRGB AnimationPlayer::resolveColorRef(const ColorRef& ref) const
    {
        switch (ref.kind) {
            case COLORREF_RGB:
                return ::Protocol::ColorRGB{ ref.rgb.r, ref.rgb.g, ref.rgb.b };

            case COLORREF_PALETTE:
            {
                ::Protocol::ColorRGB out;

                samplePalette(palette, paletteCount, ref.palette.pos, &out.r, &out.g, &out.b);

                return out;
            }

            case COLORREF_USE_COLOR:
            {
                uint8_t slot = ref.useColor.slot;

                if (slot >= BASE_COLORS_COUNT) slot = 0;

                return baseColors[slot];
            }

            default:
                return ::Protocol::ColorRGB{ 0xFF, 0xFF, 0xFF };
        }
    }

    void AnimationPlayer::resolveCurrentColors(::Protocol::ColorRGB *outFrom, ::Protocol::ColorRGB *outTo) const
    {
        const AnimationState& anim = queue[queueHead];

        *outFrom = resolveColorRef(anim.colorFrom);
        *outTo   = resolveColorRef(anim.colorTo);
    }

    // ============================================================================
    // Packet Handlers
    // ============================================================================

    void AnimationPlayer::prepare(const ::Protocol::PacketAnimationPrepare *pkt)
    {
        // Enqueue animation in the queue
        if (queueCount >= 4) {
            D_PRINTLN("[ANIM] queue full, PREPARE dropped");

            return;
        }

        uint8_t idx = (queueHead + queueCount) % 4;
        AnimationState& slot = queue[idx];

        slot.animType = pkt->animType;
        slot.group_id = pkt->group_id;
        slot.flags = pkt->flags;
        slot.transitionMs = pkt->transitionMs;
        slot.durationMs = pkt->durationMs;
        slot.colorFrom = pkt->colorFrom;
        slot.colorTo = pkt->colorTo;
        slot.param1 = pkt->param1;
        slot.param2 = pkt->param2;
        slot.startMs = 0; // filled in when animation starts

        queueCount++;
    }

    void AnimationPlayer::start(uint8_t seq_id, uint8_t group_id)
    {
        if (seq_id == lastStartSeqId) {
            return;
        }

        lastStartSeqId = seq_id;

        // Find first queued animation with matching group_id and start it
        for (uint8_t i = 0; i < queueCount; i++) {
            uint8_t idx = (queueHead + i) % 4;

            if (queue[idx].group_id == group_id) {
                // Move this animation to front and start it
                if (i > 0) {
                    // Rotate queue: move matching animation to head
                    AnimationState temp = queue[idx];

                    for (uint8_t j = idx; j != queueHead; j = (j > 0) ? j - 1 : 3) {
                        queue[j] = queue[(j > 0) ? j - 1 : 3];
                    }

                    queue[queueHead] = temp;

                    // The old running animation was displaced to position 1; discard it so
                    // it doesn't auto-restart when this new animation ends.
                    // Shift items [2..queueCount-1] left by one to fill the gap.
                    for (uint8_t k = 1; k < queueCount - 1; k++) {
                        queue[(queueHead + k) % 4] = queue[(queueHead + k + 1) % 4];
                    }

                    queueCount--;
                }

                // Start the animation
                AnimationState& anim = queue[queueHead];

                animType = anim.animType;
                groupId = anim.group_id;
                flags = anim.flags;
                transitionMs = anim.transitionMs;
                durationMs = anim.durationMs;
                startMs = millis();
                paused = false;
                pausedElapsedMs = 0;

                // Resolve FLAG_CURRENT_* — substitute live LED state for ignored fields.
                // The live color becomes an inline RGB ColorRef so resolveColorRef returns
                // it as-is without further indirection.
                if (rgbController) {
                    if (anim.flags & FLAG_CURRENT_COLOR_FROM) {
                        ::Protocol::ColorRGB c = rgbController->color();

                        anim.colorFrom = ColorRef_rgb(c.r, c.g, c.b);
                    }

                    if (anim.flags & FLAG_CURRENT_COLOR_TO) {
                        ::Protocol::ColorRGB c = rgbController->color();

                        anim.colorTo = ColorRef_rgb(c.r, c.g, c.b);
                    }
                }

                if (animType == ANIM_REACTIVE) {
                    reactiveDecayRate = anim.param1;
                    reactiveTriggerMs = startMs;
                    reactiveLevel = 0; // start idle
                }

                lastTickMs = startMs;

                return;
            }
        }
    }

    void AnimationPlayer::control(uint8_t cmd)
    {
        switch (cmd) {
            case ANIM_CTRL_STOP:
                queueCount = 0;
                queueHead = 0;
                animType = ANIM_SOLID;
                groupId = 0;
                reactiveLevel = 0;
                break;

            case ANIM_CTRL_PAUSE:

                if (animType != ANIM_SOLID) {
                    paused = true;
                    pausedElapsedMs = (uint16_t)(millis() - startMs);
                }

                break;

            case ANIM_CTRL_RESUME:

                if (paused && animType != ANIM_SOLID) {
                    paused = false;
                    startMs = millis() - pausedElapsedMs;
                }

                break;

            case ANIM_CTRL_CLEAR_QUEUE:

                if (queueCount > 0) {
                    queueCount--; // remove next queued animation
                }

                break;
        }
    }

    void AnimationPlayer::updateParams(uint8_t seq_id, uint8_t group_id, uint8_t param_type, uint8_t value, uint8_t transitionMs)
    {
        if (seq_id == lastParamsSeqId) {
            return;
        }

        lastParamsSeqId = seq_id;

        // Check if this param update applies to current animation
        if (group_id != 0 && group_id != groupId) {
            return; // not for our group
        }

        switch (param_type) {
            case PARAM_TRIGGER:

                // REACTIVE animations respond to trigger
                if (animType == ANIM_REACTIVE) {
                    reactiveLevel = value;
                    reactiveTriggerMs = millis();
                }

                break;

            case PARAM_BRIGHTNESS_MULT:
                // TODO: global brightness multiplier (future)
                break;

            case PARAM_SPEED_SCALE:
                // TODO: speed multiplier (future)
                break;
        }
    }

    // ============================================================================
    // Main Tick Function
    // ============================================================================

    void AnimationPlayer::tick()
    {
        uint16_t now = millis();

        // Frame gate: only update every ~16ms (60fps)
        if ((uint16_t)(now - lastTickMs) < 16) {
            return;
        }

        lastTickMs = now;

        // Handle animation state machine
        if (animType == ANIM_SOLID && reactiveLevel == 0) {
            // Idle — wait for START signal. Do NOT auto-drain the queue here: a
            // PREPARE may have just been received and its START general call is still
            // in flight. Draining would consume the queued animation before start()
            // can find it, causing one panel to silently miss the cycle.
            return;
        }

        // If paused, don't advance animation
        if (paused) {
            return;
        }

        // Calculate elapsed time
        uint16_t elapsed = (uint16_t)(now - startMs);

        // Check if animation is finished
        if (durationMs > 0 && elapsed >= durationMs && !(flags & FLAG_LOOP) && animType != ANIM_REACTIVE) {
            // Snap to the exact end-state before advancing so the LED reaches its
            // natural final value (e.g. breathe fades to 0) rather than freezing
            // on whatever brightness the last 16 ms tick happened to land on.
            computeFrame(durationMs);
            applyToLED();
            advanceQueue();

            return;
        }

        // Compute frame for current animation type
        computeFrame(elapsed);

        // Apply to LED
        applyToLED();
    }

    // ============================================================================
    // Animation Computation
    // ============================================================================

    void AnimationPlayer::computeFrame(uint16_t elapsed)
    {
        switch (animType) {
            case ANIM_SOLID:
                currentColor = resolveColorRef(queue[queueHead].colorTo);
                break;

            case ANIM_FADE:
                tickFade(elapsed);
                break;

            case ANIM_TRANSITION:
                tickTransition(elapsed);
                break;

            case ANIM_BREATHE:
                tickBreathe(elapsed);
                break;

            case ANIM_PULSE:
                tickPulse(elapsed);
                break;

            case ANIM_BLINK:
                tickBlink(elapsed);
                break;

            case ANIM_HUE_CYCLE:
                tickHueCycle(elapsed);
                break;

            case ANIM_STROBE:
                tickStrobe(elapsed);
                break;

            case ANIM_REACTIVE:
                tickReactive(elapsed);
                break;

            default:
                break;
        }
    }

    // ============================================================================
    // Animation Type Handlers
    // ============================================================================

    void AnimationPlayer::tickFade(uint16_t elapsed)
    {
        uint8_t progress_q8;

        if (durationMs > 0) {
            uint32_t prog = (uint32_t)elapsed * 256 / durationMs;

            progress_q8 = (prog > 255) ? 255 : (uint8_t)prog;
        } else {
            progress_q8 = 255;
        }

        ::Protocol::ColorRGB cFrom, cTo;
        resolveCurrentColors(&cFrom, &cTo);
        rgbLerp(cFrom, cTo, progress_q8, &currentColor);
    }

    void AnimationPlayer::tickTransition(uint16_t elapsed)
    {
        uint8_t progress_q8;

        if (durationMs > 0) {
            uint32_t prog = (uint32_t)elapsed * 256 / durationMs;

            progress_q8 = (prog > 255) ? 255 : (uint8_t)prog;
        } else {
            progress_q8 = 255;
        }

        ::Protocol::ColorRGB cFrom, cTo;
        resolveCurrentColors(&cFrom, &cTo);
        rgbLerp(cFrom, cTo, progress_q8, &currentColor);
    }

    void AnimationPlayer::tickBreathe(uint16_t elapsed)
    {
        if (durationMs == 0) return;

        // Wrap elapsed so FLAG_LOOP cycles correctly
        uint16_t t    = elapsed % durationMs;
        uint16_t half = durationMs / 2;

        if (half == 0) return;

        uint8_t phase_q8 = (t <= half)
        ? (uint8_t)((uint32_t)t * 255 / half)
        : (uint8_t)(((uint32_t)(durationMs - t) * 255) / half);

        uint8_t inv    = (uint8_t)(255 - phase_q8);
        uint8_t inv_sq = (uint8_t)(((uint16_t)inv * inv) >> 8);
        uint8_t ease   = (uint8_t)(255 - inv_sq);

        ::Protocol::ColorRGB cFrom, cTo;

        resolveCurrentColors(&cFrom, &cTo);
        rgbLerp(cFrom, cTo, ease, &currentColor);
    }

    void AnimationPlayer::tickPulse(uint16_t elapsed)
    {
        AnimationState& anim = queue[queueHead];
        uint8_t rise_pct = anim.param1; // 0-255
        uint8_t fall_pct = anim.param2; // 0-255
        // Clamp so rise+fall never exceed 255; hold_pct gets the remainder.
        uint16_t sum = (uint16_t)rise_pct + fall_pct;

        if (sum > 255) {
            rise_pct = (uint8_t)(255 * rise_pct / sum);
            fall_pct = (uint8_t)(255 - rise_pct);
        }

        uint8_t hold_pct = 255 - rise_pct - fall_pct;

        uint16_t rise_ms = (uint32_t)durationMs * rise_pct / 256;
        uint16_t hold_ms = (uint32_t)durationMs * hold_pct / 256;

        uint8_t progress_q8;

        if (elapsed < rise_ms) {
            // Rising phase
            progress_q8 = (uint8_t)((uint32_t)elapsed * 256 / (rise_ms + 1));
        } else if (elapsed < rise_ms + hold_ms) {
            // Hold phase
            progress_q8 = 255;
        } else {
            // Fall phase
            uint16_t fall_elapsed = elapsed - rise_ms - hold_ms;
            uint16_t fall_duration = durationMs - rise_ms - hold_ms;

            progress_q8 = 255 - (uint8_t)((uint32_t)fall_elapsed * 256 / (fall_duration + 1));
        }

        ::Protocol::ColorRGB cFrom, cTo;
        resolveCurrentColors(&cFrom, &cTo);
        rgbLerp(cFrom, cTo, progress_q8, &currentColor);
    }

    void AnimationPlayer::tickBlink(uint16_t elapsed)
    {
        AnimationState& anim = queue[queueHead];
        uint8_t period_ms = anim.param1;

        if (period_ms == 0) period_ms = 1;

        uint16_t phase = elapsed % (period_ms * 2);
        bool on = (phase < period_ms);

        ::Protocol::ColorRGB cFrom, cTo;

        resolveCurrentColors(&cFrom, &cTo);
        currentColor = on ? cTo : cFrom;
    }

    void AnimationPlayer::tickHueCycle(uint16_t elapsed)
    {
        AnimationState& anim = queue[queueHead];
        uint8_t speed = anim.param1;

        if (speed == 0) speed = 1;

        uint16_t hue_step = ((uint32_t)elapsed * speed / 10) % 1530; // 1530 = 6 * 255

        // Simple integer HSV -> RGB (6-step rainbow, no interpolation)
        uint8_t segment = hue_step / 255;
        uint8_t remainder = hue_step % 255;

        uint8_t r = 0, g = 0, b = 0;

        switch (segment) {
            case 0: r = 255;
                g = remainder;
                b = 0;
                break;                                                      // red -> yellow
            case 1: r = 255 - remainder;
                g = 255;
                b = 0;
                break;                                                      // yellow -> green
            case 2: r = 0;
                g = 255;
                b = remainder;
                break;                                                      // green -> cyan
            case 3: r = 0;
                g = 255 - remainder;
                b = 255;
                break;                                                      // cyan -> blue
            case 4: r = remainder;
                g = 0;
                b = 255;
                break;                                                      // blue -> magenta
            case 5: r = 255;
                g = 0;
                b = 255 - remainder;
                break;                                                            // magenta -> red
        }

        currentColor = { r, g, b };
    }

    void AnimationPlayer::tickStrobe(uint16_t elapsed)
    {
        AnimationState& anim = queue[queueHead];
        uint8_t hz = anim.param1;

        if (hz == 0) hz = 1;

        uint16_t period_ms = 1000 / hz;
        bool on = (elapsed % period_ms) < (period_ms / 2);

        ::Protocol::ColorRGB cTo = resolveColorRef(anim.colorTo);

        currentColor = on ? cTo : ::Protocol::ColorRGB{ 0, 0, 0 };
    }

    void AnimationPlayer::tickReactive(uint16_t elapsed)
    {
        // Decay reactiveLevel over time
        if (reactiveLevel > 0) {
            uint16_t since_trigger = (uint16_t)(millis() - reactiveTriggerMs);
            uint32_t decay_amount = (uint32_t)reactiveDecayRate * since_trigger / 1000;

            reactiveLevel = (decay_amount >= reactiveLevel) ? 0 : (uint8_t)(reactiveLevel - decay_amount);
        }

        ::Protocol::ColorRGB cFrom, cTo;

        resolveCurrentColors(&cFrom, &cTo);
        rgbLerp(cFrom, cTo, reactiveLevel, &currentColor);
    }

    // ============================================================================
    // Utilities
    // ============================================================================

    uint8_t AnimationPlayer::lerp8(uint8_t a, uint8_t b, uint8_t frac_q8)
    {
        if (a == b || frac_q8 == 0) return a;

        if (frac_q8 == 255) return b; // 255/256 ≠ 1.0 in q8, so snap to avoid off-by-one at end

        if (a < b) {
            return a + (uint8_t)(((uint32_t)(b - a) * frac_q8) >> 8);
        } else {
            return a - (uint8_t)(((uint32_t)(a - b) * frac_q8) >> 8);
        }
    }

    void AnimationPlayer::rgbLerp(::Protocol::ColorRGB a, ::Protocol::ColorRGB b, uint8_t frac_q8, ::Protocol::ColorRGB *out)
    {
        out->r = lerp8(a.r, b.r, frac_q8);
        out->g = lerp8(a.g, b.g, frac_q8);
        out->b = lerp8(a.b, b.b, frac_q8);
    }

    // ============================================================================
    // Status and Queue Management
    // ============================================================================

    void AnimationPlayer::fillStatus(::Protocol::PacketAnimationStatus *out)
    {
        out->animType = animType;
        out->group_id = groupId;
        out->elapsedMs = paused ? pausedElapsedMs : (uint16_t)(millis() - startMs);
        out->durationMs = durationMs;
        out->queueLen = (queueCount > 0) ? queueCount - 1 : 0; // queue length behind current
    }

    void AnimationPlayer::advanceQueue()
    {
        if (queueCount > 0) {
            queueHead = (queueHead + 1) % 4;
            queueCount--;
        }

        if (queueCount > 0) {
            // Start next animation
            AnimationState& next = queue[queueHead];

            animType = next.animType;
            groupId = next.group_id;
            flags = next.flags;
            transitionMs = next.transitionMs;
            durationMs = next.durationMs;
            startMs = millis();
            paused = false;
            pausedElapsedMs = 0;

            if (animType == ANIM_REACTIVE) {
                reactiveDecayRate = next.param1;
                reactiveTriggerMs = startMs;
                reactiveLevel = 0;
            }
        } else {
            // Queue empty, go idle
            animType = ANIM_SOLID;
            groupId = 0;
            reactiveLevel = 0;
        }
    }

    void AnimationPlayer::applyToLED()
    {
        if (!rgbController) {
            return;
        }

        // RGBController::updateOutputs() checks isOn and is a no-op when off,
        // so a TURN_OFF command is naturally respected without any override here.
        rgbController->color(currentColor.r, currentColor.g, currentColor.b);
    }
}  // namespace Lightnet
