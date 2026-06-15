#pragma once

// ============================================================================
// RunnerCompile — invert the spatial runner envelopes (RunnerMath.hpp) into a
// per-panel local PULSE, so a deterministic sweep can run autonomously on each
// panel instead of being streamed as 60fps SET_COLOR from the controller.
//
// Each panel's pulse is: black until its onset (startDelayMs), a single PULSE of
// `durationMs` (rise→hold→fall shaped to match the envelope), then black again
// (the panel finishes and holds its dark end state). Repetition for looping
// scenes comes from the scene-cycle barrier re-firing the step — the compiled
// pulse itself is one-shot.
//
// Pure float math (no Arduino), unit-tested in test_runner_compile against the
// onset/peak/end of the corresponding RunnerMath envelope.
// ============================================================================

#include <stdint.h>
#include <math.h>
#include "RunnerMath.hpp"

namespace Lightnet {
    struct CompiledPulse {
        bool     lit;          // false → this panel is never lit by the sweep (send nothing)
        uint16_t startDelayMs; // onset offset within the sweep
        uint16_t durationMs;   // length of the lit pulse
        uint8_t  risePct;      // PULSE param1 (0-255)
        uint8_t  fallPct;      // PULSE param2 (0-255)
    };

    static inline uint16_t rc_ms(float v)
    {
        if (v < 0.0f) v = 0.0f;

        if (v > 65535.0f) v = 65535.0f;

        return (uint16_t)(v + 0.5f);
    }

    static inline uint8_t rc_pct(float frac)
    {
        if (frac < 0.0f) frac = 0.0f;

        if (frac > 1.0f) frac = 1.0f;

        return (uint8_t)(frac * 255.0f + 0.5f);
    }

    // WAVE: triangular band, centre sweeps -w/2 → maxCoord+w/2 over [0,dur].
    // Panel `coord` is lit on t ∈ [coord, coord+w]/(maxCoord+w); peak at the midpoint.
    inline CompiledPulse compileWave(float coord, uint8_t maxCoord, uint8_t width, uint16_t dur)
    {
        CompiledPulse cp = { false, 0, 0, 127, 128 };

        if (width == 0 || dur == 0) return cp;

        float denom = (float)maxCoord + (float)width;

        if (denom <= 0.0f) return cp;

        float tOn   = coord / denom;
        float tSpan = (float)width / denom;

        cp.lit          = true;
        cp.startDelayMs = rc_ms((float)dur * tOn);
        cp.durationMs   = rc_ms((float)dur * tSpan);

        if (cp.durationMs == 0) cp.durationMs = 1;

        cp.risePct = 127; // symmetric triangle (rise→peak→fall), hold = 0
        cp.fallPct = 128;

        return cp;
    }

    // BOUNCE: like WAVE, but the band's PEAK travels only the real panel span [0, maxCoord]
    // (not -w/2 → maxCoord+w/2), so it reflects at the edges instead of sliding off-canvas.
    // One pass: centre sweeps 0 → maxCoord over [0,dur]; panel `coord` peaks at t = coord/maxCoord
    // and is lit while |centre-coord| < w/2. The caller toggles direction each cycle (bouncePhase)
    // for the back-and-forth; the t=1-forward profile equals the t=0-reverse profile (band centred
    // on the same edge), so the reflection is seamless. Edge panels get an asymmetric (clamped)
    // rise/fall window — the peak is pinned to the edge so half the triangle is off the field.
    inline CompiledPulse compileBounce(float coord, uint8_t maxCoord, uint8_t width, uint16_t dur)
    {
        CompiledPulse cp = { false, 0, 0, 127, 128 };

        if (width == 0 || dur == 0) return cp;

        float span = (float)maxCoord;

        if (span <= 0.0f) span = 1.0f; // single-coord field — degenerate, light the whole pass

        float halfW = (float)width / 2.0f;
        float tPeak = coord / span;            // centre passes this panel
        float tOn   = (coord - halfW) / span;  // band's leading edge reaches it
        float tOff  = (coord + halfW) / span;  // band's trailing edge leaves it

        if (tOn < 0.0f)  tOn  = 0.0f;

        if (tOff > 1.0f) tOff = 1.0f;

        if (tOff <= tOn) return cp;

        if (tPeak < tOn)  tPeak = tOn;         // edge panel: peak pinned to the clamped window

        if (tPeak > tOff) tPeak = tOff;

        float spanT = tOff - tOn;

        cp.lit          = true;
        cp.startDelayMs = rc_ms((float)dur * tOn);
        cp.durationMs   = rc_ms((float)dur * spanT);

        if (cp.durationMs == 0) cp.durationMs = 1;

        cp.risePct = rc_pct((tPeak - tOn) / spanT);
        cp.fallPct = rc_pct((tOff - tPeak) / spanT);

        return cp;
    }

    // CHASE: a single lit coordinate sweeping outward; panel `coord` is lit for one
    // step-tick on t ∈ [coord, coord+1]/(maxCoord+1). Near-square pulse that ends dark.
    inline CompiledPulse compileChase(uint8_t coord, uint8_t maxCoord, uint16_t dur)
    {
        CompiledPulse cp = { false, 0, 0, 8, 8 };

        if (dur == 0) return cp;

        float denom = (float)maxCoord + 1.0f;
        float tOn   = (float)coord / denom;

        cp.lit          = true;
        cp.startDelayMs = rc_ms((float)dur * tOn);
        cp.durationMs   = rc_ms((float)dur / denom);

        if (cp.durationMs == 0) cp.durationMs = 1;

        cp.risePct = 8; // crisp on/off, returns to black at the end
        cp.fallPct = 8;

        return cp;
    }

    // RIPPLE (extent-aware): panel spans radial band [near, far]; ring radius expands
    // 0 → maxCoord + w/2. Fully lit while the ring is within [near, far], soft edges of
    // width w/2 on either side. near==far reduces to the point model (triangle).
    inline CompiledPulse compileRipple(float nearC, float farC, uint8_t maxCoord, uint8_t width, uint16_t dur)
    {
        CompiledPulse cp = { false, 0, 0, 127, 128 };

        if (width == 0 || dur == 0) return cp;

        float ringW = (float)width / 2.0f;
        float denom = (float)maxCoord + ringW;

        if (denom <= 0.0f) return cp;

        float tOn      = (nearC - ringW) / denom;
        float tFullOn  = nearC / denom;
        float tFullOff = farC / denom;
        float tOff     = (farC + ringW) / denom;

        if (tOn < 0.0f)  tOn  = 0.0f;

        if (tOff > 1.0f) tOff = 1.0f;

        if (tOff <= tOn) return cp;

        float span = tOff - tOn;

        cp.lit          = true;
        cp.startDelayMs = rc_ms((float)dur * tOn);
        cp.durationMs   = rc_ms((float)dur * span);

        if (cp.durationMs == 0) cp.durationMs = 1;

        cp.risePct = rc_pct((tFullOn - tOn) / span);
        cp.fallPct = rc_pct((tOff - tFullOff) / span);

        return cp;
    }

    // ========================================================================
    // Repeating envelope — backs WHEEL's perpetual rotation (a wheel never stops
    // spinning, so it always loops via this swapped-colour FLAG_LOOP trick).
    //
    // A one-shot compile above produces black → lit → black once, holding dark at
    // the end (no FLAG_LOOP). Looping that same shape would re-enter the rise edge
    // immediately at the wrap with no rest in between — there is no "hold dark"
    // phase in tickPulse's rise→hold→fall envelope, only "hold lit".
    //
    // So instead we *swap* colorFrom/colorTo (lit → dark): the envelope now reads
    // departing → dark-gap → approaching, peaking exactly at the loop seam
    // (animElapsed wraps to 0, where progress_q8 == 0 == colorFrom == lit). Position
    // that seam at each panel's onset moment via startDelayMs, set durationMs to the
    // shared loop period, and FLAG_LOOP repeats it forever — a steady, evenly-phased
    // train with a true dark gap between passes, using the existing PULSE shape and
    // zero protocol changes.
    //
    // `phase`   — where in [0,1) of the period this panel's peak falls (its onset).
    // `halfWidth` — the visible band's half-width, also as a fraction of the period;
    //               shared symmetrically by the departing/approaching edges.
    // Asymmetric variant: independent rise/fall fractions of the period. `compileRepeating`
    // (symmetric bands) is the riseFrac==fallFrac special case.
    inline CompiledPulse compileRepeatingAsym(float phase, float riseFrac, float fallFrac, uint16_t period)
    {
        CompiledPulse cp = { false, 0, 0, 0, 0 };

        if (period == 0) return cp;

        // Defend against phase >= 1.0 due to floating-point rounding.
        // phase should always be in [0,1) but clamp/wrap here as a safety net.
        // Use fmodf for proper modulo in case of any overshoot.
        phase = fmodf(phase, 1.0f);

        if (phase < 0.0f) phase += 1.0f;

        cp.lit          = true;
        cp.startDelayMs = rc_ms(phase * (float)period);
        cp.durationMs   = period;
        cp.risePct      = rc_pct(riseFrac);
        cp.fallPct      = rc_pct(fallFrac);

        return cp;
    }

    inline CompiledPulse compileRepeating(float phase, float halfWidth, uint16_t period)
    {
        return compileRepeatingAsym(phase, halfWidth, halfWidth, period);
    }

    // WHEEL: `lines` evenly-spaced blades rotate together with period `rotationMs`,
    // so any one panel is struck once every `rotationMs / lines` — that's the loop
    // period. `turns` is the panel's bearing from the centre (0..1, one full turn);
    // folding it into one blade's angular slot (frac(turns*lines)) gives its phase
    // within that period, and `thicknessDeg` (the blade's angular width) becomes a
    // half-width fraction of the same slot (thicknessDeg*lines/720, halved already
    // by the /2 of "half"). Always loops — a wheel never stops spinning — via the
    // same swapped-colour compileRepeating engine as a repeating ripple/wave/chase.
    //
    // NB: phase needs no upper clamp. Panels near the bearing wrap point (turns ≈ 1.0)
    // get phase ≈ 1.0 and thus startDelayMs ≈ period, but because the panel treats a
    // looping layer's startDelayMs as a phase offset (not a one-shot delay), the
    // swapped-colour envelope wraps seamlessly across the loop seam — there is no
    // "no time left for the fade" failure to guard against.
    inline CompiledPulse compileWheel(float turns, uint8_t lines, uint8_t thicknessDeg, uint16_t rotationMs)
    {
        if (lines == 0 || rotationMs == 0) return CompiledPulse{ false, 0, 0, 0, 0 };

        uint16_t period = (uint16_t)(rotationMs / lines);

        if (period == 0) period = 1;

        float slot  = turns * (float)lines;
        float phase = slot - floorf(slot); // frac(turns * lines) ∈ [0,1)

        // Clamp phase to [0, 1.0) via fmodf in compileRepeating; no arbitrary 0.80
        // ceiling needed here as WHEEL always loops, so the envelope wraps seamlessly.
        float halfWidth = ((float)thicknessDeg * (float)lines) / 720.0f;

        return compileRepeating(phase, halfWidth, period);
    }

    // NB: RAIN and SPARKLE are NOT compiled here. They are stochastic particle *spawners*
    // (genuinely random, non-repeating, soft-draining) that the compiled-pulse model cannot
    // express — ScenePlayer services them over the step window, emitting one-shot drop pulses
    // on pooled group_ids. See RunnerSpawn.hpp + ScenePlayer::serviceSpawner.
}  // namespace Lightnet
