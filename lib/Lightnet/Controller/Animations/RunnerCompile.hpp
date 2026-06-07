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
}  // namespace Lightnet
