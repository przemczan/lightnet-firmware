#include "AnimationPlayer.hpp"

// Portable core: no Arduino/Debug. Logging is a no-op hook the platform may override.
#ifndef LN_ANIM_LOG
    #define LN_ANIM_LOG(msg) ((void)0)
#endif

namespace Lightnet {
    AnimationPlayer::AnimationPlayer()
        : lastStartSeqId(0xFF), lastParamsSeqId(0xFF),
        nowMs(0), lastTickMs(0), outputDirty(false), paletteCount(2)
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

    ::Protocol::ColorRGB AnimationPlayer::currentColor() const
    {
        return lastOutput;
    }

    bool AnimationPlayer::takeDirty()
    {
        bool d = outputDirty;

        outputDirty = false;

        return d;
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

        setOutput(backgroundColor);
    }

    // Direct SET_COLOR: becomes the current colour unconditionally (event-driven, ungated —
    // reproduces the panel's pre-refactor immediate LED write). Also keeps lastOutput in sync
    // so FLAG_CURRENT_COLOR_* substitution reads the true current colour. Also updates
    // backgroundColor so a later composite() with no active layers falls back to this
    // colour instead of a stale scene background.
    void AnimationPlayer::setColorDirect(const ::Protocol::ColorRGB& c)
    {
        lastOutput      = c;
        backgroundColor = c;
        outputDirty     = true;
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
        s.reactiveTriggerMs = 0;
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
        s.cur.startMs = nowMs;

        // Resolve FLAG_CURRENT_* — substitute the current composited/direct colour.
        if (s.cur.flags & FLAG_CURRENT_COLOR_FROM) {
            s.cur.colorFrom = ColorRef_rgb(lastOutput.r, lastOutput.g, lastOutput.b);
        }

        if (s.cur.flags & FLAG_CURRENT_COLOR_TO) {
            s.cur.colorTo = ColorRef_rgb(lastOutput.r, lastOutput.g, lastOutput.b);
        }

        if (s.cur.animType == ANIM_REACTIVE) {
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
            LN_ANIM_LOG("[ANIM] no free slot, PREPARE dropped");

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
        a.animates     = pkt->animates;

        s->flags |= Slot::HAS_PENDING;
    }

    void AnimationPlayer::start(uint8_t seq_id, uint8_t group_id, uint16_t now)
    {
        nowMs = now;

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

    void AnimationPlayer::control(uint8_t cmd, uint8_t group_id, uint16_t now)
    {
        nowMs = now;

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

    void AnimationPlayer::updateParams(
        uint8_t  seq_id,
        uint8_t  group_id,
        uint8_t  param_type,
        uint8_t  value,
        uint8_t /*transitionMs*/,
        uint16_t now
    )
    {
        nowMs = now;

        if (seq_id == lastParamsSeqId) {
            return;
        }

        lastParamsSeqId = seq_id;

        if (param_type != PARAM_TRIGGER) {
            return; // BRIGHTNESS_MULT / SPEED_SCALE reserved for future
        }

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

    void AnimationPlayer::tick(uint16_t now)
    {
        nowMs = now;

        // Frame gate: ~60fps
        if ((uint16_t)(now - lastTickMs) < 16) {
            return;
        }

        lastTickMs = now;

        composite();
    }

    void AnimationPlayer::composite()
    {
        uint16_t now = nowMs;

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

            uint16_t animElapsed = 0;

            if ((s.cur.flags & FLAG_LOOP) && s.cur.durationMs > 0) {
                // When looping, startDelayMs acts as a phase offset rather than a one-shot delay.
                // This ensures re-firing/syncing is seamless and eliminates the initial gap.
                uint32_t offset = (uint32_t)s.cur.durationMs - (s.cur.startDelayMs % s.cur.durationMs);

                animElapsed = (uint16_t)(((uint32_t)elapsed + offset) % s.cur.durationMs);
            } else {
                // Before its onset, a non-looping slot is transparent (layers below show through).
                if (!(s.flags & Slot::HOLDING) && elapsed < s.cur.startDelayMs) {
                    continue;
                }

                animElapsed = (uint16_t)(elapsed - s.cur.startDelayMs);

                // Finish detection (skip while paused — frozen in place).
                if (!(s.flags & Slot::PAUSED) && !(s.flags & Slot::HOLDING) && s.cur.durationMs > 0 &&
                    animElapsed >= s.cur.durationMs && s.cur.animType != ANIM_REACTIVE) {
                    s.flags |= Slot::HOLDING;

                    // Reap-on-done (spawner drops): free the slot the instant it finishes instead
                    // of holding its end-state forever, releasing the pooled group_id so panels
                    // never clog. The end state is transparent (faded to colorFrom=black, or a
                    // modifier back to identity), so freeing is visually identical to holding.
                    if (s.cur.flags & FLAG_REAP_ON_DONE) {
                        s.flags = 0; // clear OCCUPIED/STARTED/HOLDING → slot free for reuse
                        continue;    // contributes nothing this frame
                    }
                }

                if ((s.flags & Slot::HOLDING) && s.cur.durationMs > 0) {
                    animElapsed = s.cur.durationMs; // snap to the natural end state
                }
            }

            CompositeLayer& c = contrib[n];

            c.composeOrder = s.cur.composeOrder;

            ::Protocol::ColorRGB outColor{ 0, 0, 0 };

            uint8_t outValue = 0;

            computeSlotOutput(s, animElapsed, &outColor, &outValue);

            if (s.cur.animates != TARGET_COLOR) {
                c.isModifier = true;
                c.value      = outValue;
                c.op         = (s.cur.animates == TARGET_DESATURATE) ? MO_DESATURATE
                             : (s.cur.animates == TARGET_HUE)         ? MO_HUE
                             : (s.cur.animates == TARGET_INVERT)      ? MO_INVERT
                             : (s.cur.animates == TARGET_BRIGHTEN)    ? MO_BRIGHTEN
                             : (s.cur.animates == TARGET_SATURATE)    ? MO_SATURATE
                             : MO_DIM;
            } else {
                c.isModifier = false;
                c.op         = s.cur.composeMode;
                c.color      = { outColor.r, outColor.g, outColor.b };
            }

            n++;
        }

        // Nothing actively contributing this frame — fall back to the background colour
        // (the scene compositor base, or the last direct SET_COLOR outside a scene) rather
        // than leaving the LED frozen at whatever the last composited colour happened to be.
        if (n == 0) {
            setOutput(backgroundColor);

            return;
        }

        RGB8 base = { backgroundColor.r, backgroundColor.g, backgroundColor.b };
        RGB8 acc  = foldLayers(contrib, n, base);
        ::Protocol::ColorRGB out = { acc.r, acc.g, acc.b };

        setOutput(out);
    }

    void AnimationPlayer::setOutput(const ::Protocol::ColorRGB& c)
    {
        // Skip redundant updates: a held/idle layer otherwise re-drives the LED every 16ms
        // forever, which briefly disables interrupts and can perturb the I²C/pinger timing.
        // Only mark dirty when the composited colour actually changes.
        if (c.r == lastOutput.r && c.g == lastOutput.g && c.b == lastOutput.b) {
            return;
        }

        lastOutput  = c;
        outputDirty = true;
    }

    // ============================================================================
    // Per-slot colour computation
    // ============================================================================

    void AnimationPlayer::computeSlotOutput(Slot& s, uint16_t elapsed, ::Protocol::ColorRGB *outColor, uint8_t *outValue)
    {
        const AnimationState& a = s.cur;

        switch (a.animType) {
            case ANIM_SOLID:

                if (a.animates == TARGET_COLOR) {
                    *outColor = resolveColorRef(a.colorTo);
                } else {
                    *outValue = getValueFrom(a); // valueTo ignored — SOLID holds a constant
                }

                break;
            case ANIM_FADE:      tickFade(a, elapsed, outColor, outValue);
                break;
            case ANIM_TRANSITION: tickFade(a, elapsed, outColor, outValue);
                break;
            case ANIM_BREATHE:   tickBreathe(a, elapsed, outColor, outValue);
                break;
            case ANIM_PULSE:     tickPulse(a, elapsed, outColor, outValue);
                break;
            case ANIM_BLINK:     tickBlink(a, elapsed, outColor, outValue);
                break;
            case ANIM_HUE_CYCLE: tickHueCycle(a, elapsed, *outColor); // COLOR-only (rejected at parse time otherwise)
                break;
            case ANIM_STROBE:    tickStrobe(a, elapsed, outColor, outValue);
                break;
            case ANIM_REACTIVE:  tickReactive(s, outColor, outValue);
                break;
            default: break; // GAP never reaches here
        }
    }

    uint8_t AnimationPlayer::getValueFrom(const AnimationState& a) const
    {
        if (a.animates == TARGET_INVERT) return 0; // ignore stored value — full identity->inverted range

        return ColorRef_scalarValue(a.colorFrom);
    }

    uint8_t AnimationPlayer::getValueTo(const AnimationState& a) const
    {
        if (a.animates == TARGET_INVERT) return 255; // ignore stored value — full identity->inverted range

        return ColorRef_scalarValue(a.colorTo);
    }

    void AnimationPlayer::applyProgress(
        const AnimationState& a,
        uint8_t               progress_q8,
        ::Protocol::ColorRGB *outColor,
        uint8_t *             outValue
    ) const
    {
        if (a.animates == TARGET_COLOR) {
            ::Protocol::ColorRGB cFrom, cTo;

            resolveColors(a, &cFrom, &cTo);
            rgbLerp(cFrom, cTo, progress_q8, outColor);
        } else {
            *outValue = lerp8(getValueFrom(a), getValueTo(a), progress_q8);
        }
    }

    // ============================================================================
    // Animation type handlers
    // ============================================================================

    void AnimationPlayer::tickFade(const AnimationState& a, uint16_t elapsed, ::Protocol::ColorRGB *outColor, uint8_t *outValue) const
    {
        uint8_t progress_q8;

        if (a.durationMs > 0) {
            uint32_t prog = (uint32_t)elapsed * 256 / a.durationMs;

            progress_q8 = (prog > 255) ? 255 : (uint8_t)prog;
        } else {
            progress_q8 = 255;
        }

        applyProgress(a, progress_q8, outColor, outValue);
    }

    void AnimationPlayer::tickBreathe(const AnimationState& a, uint16_t elapsed, ::Protocol::ColorRGB *outColor, uint8_t *outValue) const
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

        applyProgress(a, ease, outColor, outValue);
    }

    void AnimationPlayer::tickPulse(const AnimationState& a, uint16_t elapsed, ::Protocol::ColorRGB *outColor, uint8_t *outValue) const
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

        applyProgress(a, progress_q8, outColor, outValue);
    }

    void AnimationPlayer::tickBlink(const AnimationState& a, uint16_t elapsed, ::Protocol::ColorRGB *outColor, uint8_t *outValue) const
    {
        uint8_t period_ms = a.param1;

        if (period_ms == 0) period_ms = 1;

        uint16_t phase = elapsed % (period_ms * 2);
        bool on = (phase < period_ms);

        applyProgress(a, on ? (uint8_t)255 : (uint8_t)0, outColor, outValue);
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

    void AnimationPlayer::tickStrobe(const AnimationState& a, uint16_t elapsed, ::Protocol::ColorRGB *outColor, uint8_t *outValue) const
    {
        uint8_t hz = a.param1;

        if (hz == 0) hz = 1;

        uint16_t period_ms = 1000 / hz;
        bool on = (elapsed % period_ms) < (period_ms / 2);

        if (a.animates == TARGET_COLOR) {
            ::Protocol::ColorRGB cTo = resolveColorRef(a.colorTo);

            *outColor = on ? cTo : ::Protocol::ColorRGB{ 0, 0, 0 };
        } else {
            // No "black" equivalent for a scalar — alternate valueTo/valueFrom.
            *outValue = on ? getValueTo(a) : getValueFrom(a);
        }
    }

    void AnimationPlayer::tickReactive(Slot& s, ::Protocol::ColorRGB *outColor, uint8_t *outValue) const
    {
        if (s.reactiveLevel > 0) {
            uint16_t since_trigger = (uint16_t)(nowMs - s.reactiveTriggerMs);
            uint32_t decay_amount  = (uint32_t)s.cur.param1 * since_trigger / 1000;

            s.reactiveLevel = (decay_amount >= s.reactiveLevel) ? 0 : (uint8_t)(s.reactiveLevel - decay_amount);
        }

        applyProgress(s.cur, s.reactiveLevel, outColor, outValue);
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

    void AnimationPlayer::fillStatus(::Protocol::PacketAnimationStatus *out, uint16_t now)
    {
        nowMs = now;

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
            out->elapsedMs  = (top->flags & Slot::PAUSED) ? top->pausedElapsedMs : (uint16_t)(nowMs - top->cur.startMs);
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
