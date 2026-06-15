#pragma once

// ============================================================================
// RunnerSpawn — pure helpers for the RAIN / SPARKLE particle spawner.
//
// Unlike the other runners (compiled once to a per-panel looping PULSE), rain and
// sparkle are *stochastic spawners*: ScenePlayer services them over the step window
// and emits one-shot "drop" pulses at a rate, each on a pooled group_id, that finish
// and self-reap on the panel (FLAG_REAP_ON_DONE). This buys genuinely random, non-
// repeating, seamless-by-draining behaviour the compiled-pulse model can't express.
//
// Everything here is the pure, Arduino-free math (PRNG, spawn-rate accumulator,
// group-id pool, source→leaf path, per-panel drop timing) — unit-tested natively in
// test_runner_spawn. The stateful real-time wiring lives in ScenePlayer.
// ============================================================================

#include <stdint.h>

namespace Lightnet {
    // ---- Deterministic PRNG (xorshift32). Seeded per window from millis; advanced per draw. ----
    inline uint32_t spawnRandNext(uint32_t& state)
    {
        uint32_t x = state ? state : 0x9E3779B9u; // never stick at 0

        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        state = x;

        return x;
    }

    // Uniform integer in [0, n). Returns 0 when n == 0.
    inline uint32_t spawnRandBelow(uint32_t& state, uint32_t n)
    {
        return n ? (spawnRandNext(state) % n) : 0u;
    }

    // ---- Spawn-rate accumulator -------------------------------------------------------------
    // Accumulate elapsed ms; every (1000 / wavesPerSec) ms yields one drop. Returns the number
    // due this tick (capped by maxBurst so a long stall can't dump a backlog), and consumes the
    // time it used. Decouples the spawn rate from the (variable) tick rate.
    inline uint8_t spawnDueCount(uint32_t& accumMs, uint32_t dtMs, uint8_t wavesPerSec, uint8_t maxBurst)
    {
        if (wavesPerSec == 0) return 0;

        uint32_t intervalMs = 1000u / wavesPerSec;

        if (intervalMs == 0) intervalMs = 1;

        accumMs += dtMs;

        uint8_t count = 0;

        while (accumMs >= intervalMs && count < maxBurst) {
            accumMs -= intervalMs;
            count++;
        }

        // Hit the burst cap with time to spare → drop the backlog so we don't spiral.
        if (accumMs > intervalMs) accumMs = intervalMs;

        return count;
    }

    // ---- WAVE/RIPPLE/CHASE sweep-spawn interval -----------------------------------------------
    // `density` (0-255) is a "fill rate": 0 → one sweep per `durationMs` (gapless single-file
    // train, the next sweep starts as the previous one finishes); 255 → floors at
    // `durationMs / maxConcurrent` (maxConcurrent sweeps in flight). Linear in between.
    inline uint16_t spawnSweepIntervalMs(uint16_t durationMs, uint8_t density, uint8_t maxConcurrent)
    {
        if (durationMs == 0 || maxConcurrent == 0) return 0;

        float interval = (float)durationMs * (1.0f - (float)density / 255.0f);
        float floorMs  = (float)durationMs / (float)maxConcurrent;

        if (interval < floorMs) interval = floorMs;

        uint16_t result = (uint16_t)(interval + 0.5f);

        return result ? result : 1;
    }

    // ---- Group-id pool: round-robin a contiguous block [base, base+size). Cursor persists -----
    // across the window re-fire so a new window's drops take fresh ids while the previous
    // window's drops are still draining (resetting to `base` would collide with them).
    inline uint8_t spawnPoolNext(uint16_t& cursor, uint8_t base, uint8_t size)
    {
        if (size == 0) return base;

        uint8_t g = (uint8_t)(base + (uint8_t)(cursor % size));

        cursor++;

        return g;
    }

    // ---- Source→leaf path from parent pointers ----------------------------------------------
    // Walks `leaf` up via parent[] to `root`, writing node indices into out[] as root..leaf
    // (source first). parent[root] may be root (self) or 0xFF (sentinel). Returns the path
    // length, or 0 if `leaf` isn't under `root` or the path exceeds maxLen. Pure — testable
    // with a synthetic parent array (indices are topology-node space; ScenePlayer maps to addrs).
    inline uint8_t spawnBuildPath(const uint8_t *parent, uint8_t leaf, uint8_t root, uint8_t *out, uint8_t maxLen)
    {
        uint8_t len = 0;
        uint8_t n   = leaf;

        while (true) {
            if (len >= maxLen) return 0;                // too long for the buffer

            out[len++] = n;

            if (n == root) break;

            uint8_t p = parent[n];

            if (p == n || p == 0xFF) return 0;          // hit a different root / sentinel

            n = p;
        }

        // Reverse leaf..root → root..leaf in place.
        for (uint8_t i = 0, j = (uint8_t)(len - 1); i < j; i++, j--) {
            uint8_t t = out[i];

            out[i] = out[j];
            out[j] = t;
        }

        return len;
    }

    // ---- Per-panel drop-pulse timing --------------------------------------------------------
    struct DropPulse {
        uint16_t startDelayMs;
        uint16_t durationMs;
        uint8_t  risePct; // for a colour drop (0 = instant onset)
        uint8_t  fallPct; // 255 = fade across the whole duration
    };

    // SPARKLE: a flash on one panel — instant on, fade to dark over `fadeMs`.
    inline DropPulse sparkleFlash(uint16_t fadeMs)
    {
        if (fadeMs == 0) fadeMs = 1;

        return DropPulse{ 0, fadeMs, 0, 255 };
    }

    // RAIN: the drop's head reaches path position `pos` (0 = source) at pos·ringTime and the
    // panel then fades over `widthRings`·ringTime as the tail passes; ringTime = fallMs / span,
    // span = pathLen-1 (the head crosses the whole path in `fallMs`). A tailless drop (width 0)
    // still shows a 1-ring blip.
    inline DropPulse rainDropAt(uint16_t fallMs, uint8_t widthRings, uint8_t pos, uint8_t pathLen)
    {
        uint8_t span     = (pathLen > 1) ? (uint8_t)(pathLen - 1) : 1;
        uint32_t ringTime = (uint32_t)fallMs / span;

        if (ringTime == 0) ringTime = 1;

        uint32_t rings = widthRings ? widthRings : 1;
        uint32_t dur   = rings * ringTime;
        uint32_t sd    = (uint32_t)pos * ringTime;

        if (dur > 65535u) dur = 65535u;

        if (sd > 65535u) sd = 65535u;

        return DropPulse{ (uint16_t)sd, (uint16_t)dur, 0, 255 };
    }
}  // namespace Lightnet
