#pragma once

// ============================================================================
// RunnerMath — pure brightness envelopes for the controller-side runners.
//
// Extracted from the runner tick() loops so the math is unit-testable natively
// (tick() itself can't be — it calls LNBus). Every function maps a panel's
// spatial coordinate (graph hop-distance from the source, see PanelField.hpp)
// and a time/sweep position to an 8-bit brightness.
//
// `width` is in coordinate units (rings). A width of 0 yields 0 brightness and,
// crucially, never divides — there is no division by maxCoord anywhere.
// ============================================================================

#include <stdint.h>
#include <math.h>

namespace Lightnet {
    // ---- WAVE: a triangular band centred at `center`, half-width = width/2. -----------
    inline float waveCenterAt(float t, uint8_t maxCoord)
    {
        // Sweeps from just before coord 0 to just past maxCoord. For a chain rooted at
        // one end (coord==index, maxCoord==count-1) this is the legacy wave exactly.
        return -1.5f + (float)(maxCoord + 3) * t;
    }

    inline uint8_t waveBrightness(float coord, float center, uint8_t width)
    {
        float halfW = (float)width / 2.0f;

        if (halfW <= 0.0f) return 0;

        float d = fabsf(coord - center);

        if (d >= halfW) return 0;

        return (uint8_t)(255.0f * (1.0f - d / halfW));
    }

    // ---- RIPPLE: an expanding ring at radius `radius`, ring half-width = width/2. ------
    inline float rippleRadiusAt(float t, uint8_t maxCoord)
    {
        return (float)(maxCoord + 1) * t;
    }

    inline uint8_t rippleBrightness(float coord, float radius, uint8_t width)
    {
        float ringW = (float)width / 2.0f;

        if (ringW <= 0.0f) return 0;

        float d = fabsf(coord - radius);

        if (d >= ringW) return 0;

        return (uint8_t)(255.0f * (1.0f - d / ringW));
    }

    // ---- RIPPLE (extent-aware): a panel spans the radial band [near, far] (its closest and
    // farthest distance from the origin), not a single point. The expanding ring lights it FULLY
    // while the ring radius is anywhere within that span — so two panels whose spans overlap are
    // lit together, and the closer one stays lit until the ring passes its far edge. Outside the
    // span, brightness falls off over width/2 (the same soft edge as rippleBrightness). When
    // near==far this reduces exactly to rippleBrightness (the point model used by topology ripple).
    inline uint8_t rippleBandBrightness(float near, float far, float radius, uint8_t width)
    {
        float ringW = (float)width / 2.0f;

        if (ringW <= 0.0f) return 0;

        float d;

        if (radius < near)     d = near - radius;
        else if (radius > far) d = radius - far;
        else                   return 255; // ring is crossing the panel's surface

        if (d >= ringW) return 0;

        return (uint8_t)(255.0f * (1.0f - d / ringW));
    }

    // ---- CHASE: a single lit ring sweeping outward, one coordinate at a time. ----------
    inline uint8_t chaseLitCoord(float t, uint8_t maxCoord)
    {
        int c = (int)((float)(maxCoord + 1) * t);

        if (c > (int)maxCoord) c = maxCoord;

        if (c < 0) c = 0;

        return (uint8_t)c;
    }

    inline uint8_t chaseBrightness(uint8_t coord, uint8_t litCoord)
    {
        return (coord == litCoord) ? 255 : 0;
    }
}  // namespace Lightnet
