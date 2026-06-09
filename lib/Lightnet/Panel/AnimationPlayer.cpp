#include "AnimationPlayer.hpp"
#include "../Utils/Debug.hpp"
#include "Arduino.h"

namespace Lightnet {
    AnimationPlayer::AnimationPlayer()
        : lastStartSeqId(0xFF), lastParamsSeqId(0xFF),
        rgbController(nullptr), lastTickMs(0), paletteCount(2)
    {
        for (uint8_t i = 0; i < MAX_ANIM_SLOTS; i++) {
            clearSlot(slots[i]);
        }

        lastOutput      = { 0, 0, 0 };
        backgroundColor = { 0, 0, 0 };

        // Default palette: white at both ends so any palette-position lookup returns white
        // until the controller pushes a real palette.
        palette[0] = { 0, 0xFF, 0xFF, 0xFF };
        palette[1] = { 255, 0xFF, 0xFF, 0xFF };

        // Default base colors: white / black / black.
        baseColors[0] = { 0xFF, 0xFF, 0xFF };
        baseColors[1] = { 0x00, 0x00, 0x00 };
        baseColors[2] = { 0x00, 0x00, 0x00 };
    }

    // ============================================================================
    // Palette / base colors
    // ============================================================================

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

    void AnimationPlayer::setBackground(const ::Protocol::ColorRGB& c)
    {
        backgroundColor = c;

        // Display it immediately on an idle panel so untouched panels show the background
        // at scene start. Active panels fold from it on their next composite tick.
        for (uint8_t i = 0; i < MAX_ANIM_SLOTS; i++) {
            if (slots[i].flags & Slot::OCCUPIED) return;
        }

        applyToLED(backgroundColor);
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

    void AnimationPlayer::resolveColors(const AnimationState& a, ::Protocol::ColorRGB *outFrom, ::Protocol::ColorRGB *outTo) const
    {
        *outFrom = resolveColorRef(a.colorFrom);
        *outTo   = resolveColorRef(a.colorTo);
    }

    // ============================================================================
    // Slot management
    // ============================================================================

    void AnimationPlayer::clearSlot(Slot& s)
    {
        s.flags             = 0;
        s.pausedElapsedMs   = 0;
        s.groupId           = 0;
        s.reactiveLevel     = 0;
        s.reactiveDecayRate = 0;
        s.reactiveTriggerMs = 0;
        s.outColor          = { 0, 0, 0 };
    }

    AnimationPlayer::Slot *AnimationPlayer::findSlot(uint8_t group_id)
    {
        for (uint8_t i = 0; i < MAX_ANIM_SLOTS; i++) {
            if ((slots[i].flags & Slot::OCCUPIED) && slots[i].groupId == group_id) {
                return &slots[i];
            }
        }

        return nullptr;
    }

    AnimationPlayer::Slot *AnimationPlayer::allocSlot(uint8_t group_id)
    {
        Slot *existing = findSlot(group_id);

        if (existing) return existing;

        for (uint8_t i = 0; i < MAX_ANIM_SLOTS; i++) {
            if (!(slots[i].flags & Slot::OCCUPIED)) {
                clearSlot(slots[i]);
                slots[i].flags |= Slot::OCCUPIED;
                slots[i].groupId  = group_id;

                return &slots[i];
            }
        }

        return nullptr;
    }

    void AnimationPlayer::activatePending(Slot& s)
    {
        s.cur        = s.pending;
        s.flags &= ~Slot::HAS_PENDING;
        s.flags |= Slot::STARTED;
        s.flags &= ~Slot::HOLDING;
        s.flags &= ~Slot::PAUSED;
        s.pausedElapsedMs = 0;
        s.cur.startMs = (uint16_t)millis();

        // Resolve FLAG_CURRENT_* — substitute the live composited LED state.
        if (rgbController) {
            if (s.cur.flags & FLAG_CURRENT_COLOR_FROM) {
                ::Protocol::ColorRGB c = rgbController->color();

                s.cur.colorFrom = ColorRef_rgb(c.r, c.g, c.b);
            }

            if (s.cur.flags & FLAG_CURRENT_COLOR_TO) {
                ::Protocol::ColorRGB c = rgbController->color();

                s.cur.colorTo = ColorRef_rgb(c.r, c.g, c.b);
            }
        }

        if (s.cur.animType == ANIM_REACTIVE) {
            s.reactiveDecayRate = s.cur.param1;
            s.reactiveTriggerMs = s.cur.startMs;
            s.reactiveLevel     = 0;
        }
    }

    // ============================================================================
    // Packet handlers
    // ============================================================================

    void AnimationPlayer::prepare(const ::Protocol::PacketAnimationPrepare *pkt)
    {
        Slot *s = allocSlot(pkt->group_id);

        if (!s) {
            D_PRINTLN("[ANIM] no free slot, PREPARE dropped");

            return;
        }

        AnimationState& a = s->pending;

        a.animType     = pkt->animType;
        a.group_id     = pkt->group_id;
        a.flags        = pkt->flags;
        a.transitionMs = pkt->transitionMs;
        a.durationMs   = pkt->durationMs;
        a.startMs      = 0;
        a.colorFrom    = pkt->colorFrom;
        a.colorTo      = pkt->colorTo;
        a.param1       = pkt->param1;
        a.param2       = pkt->param2;
        a.composeMode  = pkt->composeMode;
        a.composeOrder = pkt->composeOrder;
        a.startDelayMs = pkt->startDelayMs;

        s->flags |= Slot::HAS_PENDING;
    }

    void AnimationPlayer::start(uint8_t seq_id, uint8_t group_id)
    {
        if (seq_id == lastStartSeqId) {
            return;
        }

        lastStartSeqId = seq_id;

        Slot *s = findSlot(group_id);

        if (!s || !(s->flags & Slot::HAS_PENDING)) {
            return;
        }

        activatePending(*s);
    }

    void AnimationPlayer::control(uint8_t cmd, uint8_t group_id)
    {
        uint16_t now = (uint16_t)millis();

        for (uint8_t i = 0; i < MAX_ANIM_SLOTS; i++) {
            Slot& s = slots[i];

            if (!(s.flags & Slot::OCCUPIED)) continue;

            if (group_id != 0 && s.groupId != group_id) continue;

            switch (cmd) {
                case ANIM_CTRL_STOP:
                    clearSlot(s);
                    break;

                case ANIM_CTRL_PAUSE:

                    if ((s.flags & Slot::STARTED) && !(s.flags & Slot::PAUSED)) {
                        s.flags |= Slot::PAUSED;
                        s.pausedElapsedMs = (uint16_t)(now - s.cur.startMs);
                    }

                    break;

                case ANIM_CTRL_RESUME:

                    if (s.flags & Slot::PAUSED) {
                        s.flags &= ~Slot::PAUSED;
                        s.cur.startMs = (uint16_t)(now - s.pausedElapsedMs);
                    }

                    break;

                case ANIM_CTRL_CLEAR_QUEUE:
                    s.flags &= ~Slot::HAS_PENDING;
                    break;
            }
        }
    }

    void AnimationPlayer::updateParams(uint8_t seq_id, uint8_t group_id, uint8_t param_type, uint8_t value, uint8_t /*transitionMs*/)
    {
        if (seq_id == lastParamsSeqId) {
            return;
        }

        lastParamsSeqId = seq_id;

        if (param_type != PARAM_TRIGGER) {
            return; // BRIGHTNESS_MULT / SPEED_SCALE reserved for future
        }

        uint16_t now = (uint16_t)millis();

        for (uint8_t i = 0; i < MAX_ANIM_SLOTS; i++) {
            Slot& s = slots[i];

            if (!(s.flags & Slot::OCCUPIED) || !(s.flags & Slot::STARTED)) continue;

            if (group_id != 0 && s.groupId != group_id) continue;

            if (s.cur.animType == ANIM_REACTIVE) {
                s.reactiveLevel     = value;
                s.reactiveTriggerMs = now;
            }
        }
    }

    // ============================================================================
    // Main tick + compositor
    // ============================================================================

    void AnimationPlayer::tick()
    {
        uint16_t now = (uint16_t)millis();

        // Frame gate: ~60fps
        if ((uint16_t)(now - lastTickMs) < 16) {
            return;
        }

        lastTickMs = now;

        composite();
    }

    void AnimationPlayer::composite()
    {
        uint16_t now = (uint16_t)millis();

        // Resolve each started, non-transparent slot into one contribution; the shared
        // foldLayers() then sorts by composeOrder and blends/modifies onto black.
        CompositeLayer contrib[MAX_ANIM_SLOTS];
        uint8_t n = 0;

        for (uint8_t i = 0; i < MAX_ANIM_SLOTS; i++) {
            Slot& s = slots[i];

            if (!(s.flags & Slot::OCCUPIED) || !(s.flags & Slot::STARTED)) continue;

            uint16_t elapsed = (s.flags & Slot::PAUSED)
                ? s.pausedElapsedMs
                : (uint16_t)(now - s.cur.startMs);

            // Before its onset, a slot is transparent (layers below show through).
            if (!(s.flags & Slot::HOLDING) && elapsed < s.cur.startDelayMs) {
                continue;
            }

            uint16_t animElapsed = (elapsed >= s.cur.startDelayMs)
                ? (uint16_t)(elapsed - s.cur.startDelayMs)
                : 0;

            // Finish detection (skip while paused — frozen in place).
            if (!(s.flags & Slot::PAUSED) && !(s.flags & Slot::HOLDING) && s.cur.durationMs > 0 &&
                animElapsed >= s.cur.durationMs &&
                !(s.cur.flags & FLAG_LOOP) && s.cur.animType != ANIM_REACTIVE) {
                s.flags |= Slot::HOLDING;
            }

            if ((s.flags & Slot::HOLDING) && s.cur.durationMs > 0) {
                animElapsed = s.cur.durationMs; // snap to the natural end state
            } else if ((s.cur.flags & FLAG_LOOP) && s.cur.durationMs > 0) {
                animElapsed = (uint16_t)(animElapsed % s.cur.durationMs); // repeat envelope
            }

            CompositeLayer& c = contrib[n];

            c.composeOrder = s.cur.composeOrder;

            if (isModifierType(s.cur.animType)) {
                c.isModifier = true;
                c.value      = modifierValue(s, animElapsed);
                c.op         = (s.cur.animType == ANIM_MOD_SATURATION) ? MO_SATURATION
                             : (s.cur.animType == ANIM_MOD_HUE_SHIFT)  ? MO_HUE
                             : (s.cur.animType == ANIM_MOD_INVERT)     ? MO_INVERT
                             : MO_BRIGHTNESS;
            } else {
                computeSlotColor(s, animElapsed);

                c.isModifier = false;
                c.op         = s.cur.composeMode;
                c.color      = { s.outColor.r, s.outColor.g, s.outColor.b };
            }

            n++;
        }

        if (n == 0) return; // nothing active → leave the LED (idle background / direct SET_COLOR)

        RGB8 base = { backgroundColor.r, backgroundColor.g, backgroundColor.b };
        RGB8 acc  = foldLayers(contrib, n, base);
        ::Protocol::ColorRGB out = { acc.r, acc.g, acc.b };

        applyToLED(out);
    }

    void AnimationPlayer::applyToLED(const ::Protocol::ColorRGB& c)
    {
        // Skip redundant writes: a held/idle layer otherwise re-drives FastLED.showColor()
        // every 16ms forever, which briefly disables interrupts and can perturb the I²C/pinger
        // timing. Only write when the composited colour actually changes.
        if (c.r == lastOutput.r && c.g == lastOutput.g && c.b == lastOutput.b) {
            return;
        }

        lastOutput = c;

        if (!rgbController) {
            return;
        }

        // RGBController::updateOutputs() is a no-op when off, so TURN_OFF is respected.
        rgbController->color(c.r, c.g, c.b);
    }

    // ============================================================================
    // Per-slot colour computation
    // ============================================================================

    void AnimationPlayer::computeSlotColor(Slot& s, uint16_t elapsed)
    {
        const AnimationState& a = s.cur;

        switch (a.animType) {
            case ANIM_SOLID:     s.outColor = resolveColorRef(a.colorTo);
                break;
            case ANIM_FADE:      tickFade(a, elapsed, s.outColor);
                break;
            case ANIM_TRANSITION: tickFade(a, elapsed, s.outColor);
                break;
            case ANIM_BREATHE:   tickBreathe(a, elapsed, s.outColor);
                break;
            case ANIM_PULSE:     tickPulse(a, elapsed, s.outColor);
                break;
            case ANIM_BLINK:     tickBlink(a, elapsed, s.outColor);
                break;
            case ANIM_HUE_CYCLE: tickHueCycle(a, elapsed, s.outColor);
                break;
            case ANIM_STROBE:    tickStrobe(a, elapsed, s.outColor);
                break;
            case ANIM_REACTIVE:  tickReactive(s, s.outColor);
                break;
            default: break; // GAP / modifiers never reach here as source
        }
    }

    uint8_t AnimationPlayer::modifierValue(const Slot& s, uint16_t elapsed) const
    {
        const AnimationState& a = s.cur;

        if (a.durationMs == 0) {
            return a.param2; // infinite modifier holds its target (identity)
        }

        uint32_t prog = (uint32_t)elapsed * 256 / a.durationMs;
        uint8_t q8   = (prog > 255) ? 255 : (uint8_t)prog;

        if (a.flags & FLAG_MOD_BELL) {
            // Symmetric triangle: identity → peak → identity
            uint16_t bprog = (q8 < 128) ? (uint16_t)q8 * 2 : (uint16_t)(255 - q8) * 2;
            uint8_t q8b   = (bprog > 255) ? 255 : (uint8_t)bprog;

            return lerp8(a.param2, a.param1, q8b);
        }

        if (a.flags & FLAG_MOD_RISE) {
            // identity → peak
            return lerp8(a.param2, a.param1, q8);
        }

        // Default: fall — peak → identity
        return lerp8(a.param1, a.param2, q8);
    }

    // ============================================================================
    // Animation type handlers
    // ============================================================================

    void AnimationPlayer::tickFade(const AnimationState& a, uint16_t elapsed, ::Protocol::ColorRGB& out) const
    {
        uint8_t progress_q8;

        if (a.durationMs > 0) {
            uint32_t prog = (uint32_t)elapsed * 256 / a.durationMs;

            progress_q8 = (prog > 255) ? 255 : (uint8_t)prog;
        } else {
            progress_q8 = 255;
        }

        ::Protocol::ColorRGB cFrom, cTo;
        resolveColors(a, &cFrom, &cTo);
        rgbLerp(cFrom, cTo, progress_q8, &out);
    }

    void AnimationPlayer::tickBreathe(const AnimationState& a, uint16_t elapsed, ::Protocol::ColorRGB& out) const
    {
        if (a.durationMs == 0) return;

        uint16_t t    = elapsed % a.durationMs;
        uint16_t half = a.durationMs / 2;

        if (half == 0) return;

        uint8_t phase_q8 = (t <= half)
        ? (uint8_t)((uint32_t)t * 255 / half)
        : (uint8_t)(((uint32_t)(a.durationMs - t) * 255) / half);

        uint8_t inv    = (uint8_t)(255 - phase_q8);
        uint8_t inv_sq = (uint8_t)(((uint16_t)inv * inv) >> 8);
        uint8_t ease   = (uint8_t)(255 - inv_sq);

        ::Protocol::ColorRGB cFrom, cTo;

        resolveColors(a, &cFrom, &cTo);
        rgbLerp(cFrom, cTo, ease, &out);
    }

    void AnimationPlayer::tickPulse(const AnimationState& a, uint16_t elapsed, ::Protocol::ColorRGB& out) const
    {
        uint8_t rise_pct = a.param1; // 0-255
        uint8_t fall_pct = a.param2; // 0-255
        uint16_t sum     = (uint16_t)rise_pct + fall_pct;

        if (sum > 255) {
            rise_pct = (uint8_t)(255 * rise_pct / sum);
            fall_pct = (uint8_t)(255 - rise_pct);
        }

        uint8_t hold_pct = 255 - rise_pct - fall_pct;

        uint16_t rise_ms = (uint32_t)a.durationMs * rise_pct / 256;
        uint16_t hold_ms = (uint32_t)a.durationMs * hold_pct / 256;

        uint8_t progress_q8;

        if (elapsed < rise_ms) {
            progress_q8 = (uint8_t)((uint32_t)elapsed * 256 / (rise_ms + 1));
        } else if (elapsed < rise_ms + hold_ms) {
            progress_q8 = 255;
        } else {
            uint16_t fall_elapsed  = elapsed - rise_ms - hold_ms;
            uint16_t fall_duration = a.durationMs - rise_ms - hold_ms;

            progress_q8 = 255 - (uint8_t)((uint32_t)fall_elapsed * 256 / (fall_duration + 1));
        }

        ::Protocol::ColorRGB cFrom, cTo;
        resolveColors(a, &cFrom, &cTo);
        rgbLerp(cFrom, cTo, progress_q8, &out);
    }

    void AnimationPlayer::tickBlink(const AnimationState& a, uint16_t elapsed, ::Protocol::ColorRGB& out) const
    {
        uint8_t period_ms = a.param1;

        if (period_ms == 0) period_ms = 1;

        uint16_t phase = elapsed % (period_ms * 2);
        bool on = (phase < period_ms);

        ::Protocol::ColorRGB cFrom, cTo;

        resolveColors(a, &cFrom, &cTo);
        out = on ? cTo : cFrom;
    }

    void AnimationPlayer::tickHueCycle(const AnimationState& a, uint16_t elapsed, ::Protocol::ColorRGB& out) const
    {
        uint8_t speed = a.param1;

        if (speed == 0) speed = 1;

        uint16_t hue_step = ((uint32_t)elapsed * speed / 10) % 1530; // 1530 = 6 * 255

        uint8_t segment   = hue_step / 255;
        uint8_t remainder = hue_step % 255;

        uint8_t r = 0, g = 0, b = 0;

        switch (segment) {
            case 0: r = 255;
                g = remainder;
                b = 0;
                break;
            case 1: r = 255 - remainder;
                g = 255;
                b = 0;
                break;
            case 2: r = 0;
                g = 255;
                b = remainder;
                break;
            case 3: r = 0;
                g = 255 - remainder;
                b = 255;
                break;
            case 4: r = remainder;
                g = 0;
                b = 255;
                break;
            case 5: r = 255;
                g = 0;
                b = 255 - remainder;
                break;
        }

        out = { r, g, b };
    }

    void AnimationPlayer::tickStrobe(const AnimationState& a, uint16_t elapsed, ::Protocol::ColorRGB& out) const
    {
        uint8_t hz = a.param1;

        if (hz == 0) hz = 1;

        uint16_t period_ms = 1000 / hz;
        bool on = (elapsed % period_ms) < (period_ms / 2);

        ::Protocol::ColorRGB cTo = resolveColorRef(a.colorTo);

        out = on ? cTo : ::Protocol::ColorRGB{ 0, 0, 0 };
    }

    void AnimationPlayer::tickReactive(Slot& s, ::Protocol::ColorRGB& out) const
    {
        if (s.reactiveLevel > 0) {
            uint16_t since_trigger = (uint16_t)((uint16_t)millis() - s.reactiveTriggerMs);
            uint32_t decay_amount  = (uint32_t)s.reactiveDecayRate * since_trigger / 1000;

            s.reactiveLevel = (decay_amount >= s.reactiveLevel) ? 0 : (uint8_t)(s.reactiveLevel - decay_amount);
        }

        ::Protocol::ColorRGB cFrom, cTo;
        resolveColors(s.cur, &cFrom, &cTo);
        rgbLerp(cFrom, cTo, s.reactiveLevel, &out);
    }

    // ============================================================================
    // Status
    // ============================================================================

    bool AnimationPlayer::isAnimating() const
    {
        for (uint8_t i = 0; i < MAX_ANIM_SLOTS; i++) {
            const Slot& s = slots[i];

            if (!(s.flags & Slot::OCCUPIED) || !(s.flags & Slot::STARTED)) continue;

            if (s.cur.animType != ANIM_SOLID || s.reactiveLevel > 0) return true;
        }

        return false;
    }

    uint8_t AnimationPlayer::getAnimType() const
    {
        uint8_t best = ANIM_SOLID;
        int16_t bestOrder = -1;

        for (uint8_t i = 0; i < MAX_ANIM_SLOTS; i++) {
            const Slot& s = slots[i];

            if (!(s.flags & Slot::OCCUPIED) || !(s.flags & Slot::STARTED)) continue;

            if ((int16_t)s.cur.composeOrder > bestOrder) {
                bestOrder = s.cur.composeOrder;
                best      = s.cur.animType;
            }
        }

        return best;
    }

    uint8_t AnimationPlayer::getGroupId() const
    {
        uint8_t best = 0;
        int16_t bestOrder = -1;

        for (uint8_t i = 0; i < MAX_ANIM_SLOTS; i++) {
            const Slot& s = slots[i];

            if (!(s.flags & Slot::OCCUPIED) || !(s.flags & Slot::STARTED)) continue;

            if ((int16_t)s.cur.composeOrder > bestOrder) {
                bestOrder = s.cur.composeOrder;
                best      = s.groupId;
            }
        }

        return best;
    }

    void AnimationPlayer::fillStatus(::Protocol::PacketAnimationStatus *out)
    {
        const Slot *top = nullptr;
        int16_t bestOrder = -1;
        uint8_t occupiedCount = 0;

        for (uint8_t i = 0; i < MAX_ANIM_SLOTS; i++) {
            const Slot& s = slots[i];

            if (!(s.flags & Slot::OCCUPIED) || !(s.flags & Slot::STARTED)) continue;

            occupiedCount++;

            if ((int16_t)s.cur.composeOrder > bestOrder) {
                bestOrder = s.cur.composeOrder;
                top       = &s;
            }
        }

        if (top) {
            out->animType   = top->cur.animType;
            out->group_id   = top->groupId;
            out->elapsedMs  = (top->flags & Slot::PAUSED) ? top->pausedElapsedMs : (uint16_t)((uint16_t)millis() - top->cur.startMs);
            out->durationMs = top->cur.durationMs;
            out->queueLen   = (occupiedCount > 0) ? (uint8_t)(occupiedCount - 1) : 0;
        } else {
            out->animType   = ANIM_SOLID;
            out->group_id   = 0;
            out->elapsedMs  = 0;
            out->durationMs = 0;
            out->queueLen   = 0;
        }
    }

    // ============================================================================
    // Utilities
    // ============================================================================

    uint8_t AnimationPlayer::lerp8(uint8_t a, uint8_t b, uint8_t frac_q8) const
    {
        if (a == b || frac_q8 == 0) return a;

        if (frac_q8 == 255) return b;

        if (a < b) {
            return a + (uint8_t)(((uint32_t)(b - a) * frac_q8) >> 8);
        } else {
            return a - (uint8_t)(((uint32_t)(a - b) * frac_q8) >> 8);
        }
    }

    void AnimationPlayer::rgbLerp(::Protocol::ColorRGB a, ::Protocol::ColorRGB b, uint8_t frac_q8, ::Protocol::ColorRGB *out) const
    {
        out->r = lerp8(a.r, b.r, frac_q8);
        out->g = lerp8(a.g, b.g, frac_q8);
        out->b = lerp8(a.b, b.b, frac_q8);
    }
}  // namespace Lightnet
