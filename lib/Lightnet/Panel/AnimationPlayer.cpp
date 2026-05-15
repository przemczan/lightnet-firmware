#include "AnimationPlayer.hpp"
#include "Arduino.h"

namespace Lightnet {

AnimationPlayer::AnimationPlayer()
    : queueHead(0), queueCount(0), animType(ANIM_SOLID), groupId(0), flags(0),
      transitionMs(0), durationMs(0), startMs(0), paused(false), pausedElapsedMs(0),
      lastSeqId(0xFF), reactiveLevel(0), reactiveDecayRate(0), reactiveTriggerMs(0),
      rgbController(nullptr), lastTickMs(0)
{
    currentColor = {0, 0, 0};
    currentBrightness = 0;
}

// ============================================================================
// Packet Handlers
// ============================================================================

void AnimationPlayer::prepare(const ::Protocol::PacketAnimationPrepare* pkt)
{
    // Enqueue animation in the queue
    if (queueCount >= 4) {
        return;  // queue full, drop
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
    slot.brightnessFrom = pkt->brightnessFrom;
    slot.brightnessTo = pkt->brightnessTo;
    slot.param1 = pkt->param1;
    slot.param2 = pkt->param2;
    slot.startMs = 0;  // filled in when animation starts

    queueCount++;
}

void AnimationPlayer::start(uint8_t seq_id, uint8_t group_id)
{
    // Ignore duplicate General Calls
    if (seq_id == lastSeqId) {
        return;
    }
    lastSeqId = seq_id;

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

            if (animType == ANIM_REACTIVE) {
                reactiveDecayRate = anim.param1;
                reactiveTriggerMs = startMs;
                reactiveLevel = 0;  // start idle
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
                queueCount--;  // remove next queued animation
            }
            break;
    }
}

void AnimationPlayer::updateParams(uint8_t seq_id, uint8_t group_id, uint8_t param_type, uint8_t value, uint8_t transitionMs)
{
    // Duplicate guard
    if (seq_id == lastSeqId) {
        return;
    }
    lastSeqId = seq_id;

    // Check if this param update applies to current animation
    if (group_id != 0 && group_id != groupId) {
        return;  // not for our group
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
        // No animation running, try to advance queue
        if (queueCount > 0) {
            advanceQueue();
        }
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
        // Animation finished
        advanceQueue();
        return;
    }

    // Compute frame for current animation type
    computeFrame();

    // Apply to LED
    applyToLED();
}

// ============================================================================
// Animation Computation
// ============================================================================

void AnimationPlayer::computeFrame()
{
    uint16_t now = millis();
    uint16_t elapsed = paused ? pausedElapsedMs : (uint16_t)(now - startMs);

    switch (animType) {
        case ANIM_SOLID:
            // Already set via colorTo/brightnessTo in PREPARE
            currentColor = queue[queueHead].colorTo;
            currentBrightness = queue[queueHead].brightnessTo;
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
    AnimationState& anim = queue[queueHead];

    uint8_t progress_q8;
    if (durationMs > 0) {
        progress_q8 = (uint8_t)((uint32_t)elapsed * 256 / durationMs);
        if (progress_q8 > 255) progress_q8 = 255;
    } else {
        progress_q8 = 255;
    }

    currentColor = anim.colorTo;  // color doesn't change in FADE
    currentBrightness = lerp8(anim.brightnessFrom, anim.brightnessTo, progress_q8);
}

void AnimationPlayer::tickTransition(uint16_t elapsed)
{
    AnimationState& anim = queue[queueHead];

    uint8_t progress_q8;
    if (durationMs > 0) {
        progress_q8 = (uint8_t)((uint32_t)elapsed * 256 / durationMs);
        if (progress_q8 > 255) progress_q8 = 255;
    } else {
        progress_q8 = 255;
    }

    rgbLerp(anim.colorFrom, anim.colorTo, progress_q8, &currentColor);
    currentBrightness = lerp8(anim.brightnessFrom, anim.brightnessTo, progress_q8);
}

void AnimationPlayer::tickBreathe(uint16_t elapsed)
{
    AnimationState& anim = queue[queueHead];

    // Sinusoidal breathing using parabolic approximation
    // at 0ms: min, at midpoint: max, at duration: min
    uint16_t half = durationMs / 2;
    uint8_t phase = (elapsed <= half) ? (uint8_t)((uint32_t)elapsed * 256 / half)
                                       : (uint8_t)(((uint32_t)(durationMs - elapsed) * 256) / half);

    // Simple parabolic: brightness = base + range * (1 - (phase/256)^2)
    uint16_t inv_phase = 256 - phase;
    uint16_t inv_sq = ((uint32_t)inv_phase * inv_phase) >> 8;  // q8
    uint8_t breath_q8 = (uint8_t)(256 - inv_sq);

    uint8_t minBr = anim.brightnessFrom;
    uint8_t maxBr = anim.brightnessTo;
    currentBrightness = minBr + ((uint32_t)(maxBr - minBr) * breath_q8 >> 8);
    currentColor = anim.colorTo;
}

void AnimationPlayer::tickPulse(uint16_t elapsed)
{
    AnimationState& anim = queue[queueHead];
    uint8_t rise_pct = anim.param1;   // 0-255
    uint8_t fall_pct = anim.param2;   // 0-255
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

    currentColor = anim.colorTo;
    currentBrightness = lerp8(anim.brightnessFrom, anim.brightnessTo, progress_q8);
}

void AnimationPlayer::tickBlink(uint16_t elapsed)
{
    AnimationState& anim = queue[queueHead];
    uint8_t period_ms = anim.param1;
    if (period_ms == 0) period_ms = 1;

    uint16_t phase = elapsed % (period_ms * 2);
    bool on = (phase < period_ms);

    currentColor = anim.colorTo;
    currentBrightness = on ? anim.brightnessTo : anim.brightnessFrom;
}

void AnimationPlayer::tickHueCycle(uint16_t elapsed)
{
    AnimationState& anim = queue[queueHead];
    uint8_t speed = anim.param1;
    if (speed == 0) speed = 1;

    uint16_t hue_step = ((uint32_t)elapsed * speed / 10) % 1530;  // 1530 = 6 * 255

    // Simple integer HSV -> RGB (6-step rainbow, no interpolation)
    uint8_t segment = hue_step / 255;
    uint8_t remainder = hue_step % 255;

    uint8_t r = 0, g = 0, b = 0;
    switch (segment) {
        case 0: r = 255;          g = remainder;     b = 0;         break;  // red -> yellow
        case 1: r = 255 - remainder; g = 255;        b = 0;         break;  // yellow -> green
        case 2: r = 0;            g = 255;           b = remainder; break;  // green -> cyan
        case 3: r = 0;            g = 255 - remainder; b = 255;     break;  // cyan -> blue
        case 4: r = remainder;    g = 0;             b = 255;       break;  // blue -> magenta
        case 5: r = 255;          g = 0;             b = 255 - remainder; break;  // magenta -> red
    }

    currentColor = {r, g, b};
    currentBrightness = anim.brightnessTo;
}

void AnimationPlayer::tickStrobe(uint16_t elapsed)
{
    AnimationState& anim = queue[queueHead];
    uint8_t hz = anim.param1;
    if (hz == 0) hz = 1;

    uint16_t period_ms = 1000 / hz;
    bool on = (elapsed % period_ms) < (period_ms / 2);

    currentColor = anim.colorTo;
    currentBrightness = on ? anim.brightnessTo : 0;
}

void AnimationPlayer::tickReactive(uint16_t elapsed)
{
    AnimationState& anim = queue[queueHead];

    // Decay reactiveLevel over time
    if (reactiveLevel > 0) {
        uint16_t since_trigger = (uint16_t)(millis() - reactiveTriggerMs);
        uint32_t decay_amount = (uint32_t)reactiveDecayRate * since_trigger / 1000;
        reactiveLevel = (decay_amount >= reactiveLevel) ? 0 : (uint8_t)(reactiveLevel - decay_amount);
    }

    // Blend base color toward peak based on reactiveLevel
    currentColor.r = anim.colorFrom.r + ((uint32_t)(anim.colorTo.r - anim.colorFrom.r) * reactiveLevel >> 8);
    currentColor.g = anim.colorFrom.g + ((uint32_t)(anim.colorTo.g - anim.colorFrom.g) * reactiveLevel >> 8);
    currentColor.b = anim.colorFrom.b + ((uint32_t)(anim.colorTo.b - anim.colorFrom.b) * reactiveLevel >> 8);

    uint8_t br_from = anim.brightnessFrom;
    uint8_t br_to = anim.brightnessTo;
    currentBrightness = br_from + ((uint32_t)(br_to - br_from) * reactiveLevel >> 8);
}

// ============================================================================
// Utilities
// ============================================================================

uint8_t AnimationPlayer::lerp8(uint8_t a, uint8_t b, uint8_t frac_q8)
{
    if (a == b) return a;
    if (a < b) {
        return a + (uint8_t)(((uint32_t)(b - a) * frac_q8) >> 8);
    } else {
        return a - (uint8_t)(((uint32_t)(a - b) * frac_q8) >> 8);
    }
}

void AnimationPlayer::rgbLerp(::Protocol::ColorRGB a, ::Protocol::ColorRGB b, uint8_t frac_q8, ::Protocol::ColorRGB* out)
{
    out->r = lerp8(a.r, b.r, frac_q8);
    out->g = lerp8(a.g, b.g, frac_q8);
    out->b = lerp8(a.b, b.b, frac_q8);
}

// ============================================================================
// Status and Queue Management
// ============================================================================

void AnimationPlayer::fillStatus(::Protocol::PacketAnimationStatus* out)
{
    out->animType = animType;
    out->group_id = groupId;
    out->elapsedMs = paused ? pausedElapsedMs : (uint16_t)(millis() - startMs);
    out->durationMs = durationMs;
    out->queueLen = (queueCount > 0) ? queueCount - 1 : 0;  // queue length behind current
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

    // Apply current animation values to the LED
    rgbController->color(currentColor.r, currentColor.g, currentColor.b);
    rgbController->brightness(currentBrightness);

    // Ensure LED is on if brightness > 0
    if (currentBrightness > 0) {
        rgbController->turnOn();
    } else {
        rgbController->turnOff();
    }
}

}  // namespace Lightnet
