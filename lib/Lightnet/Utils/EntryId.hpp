#pragma once
// Entry ID helpers — pure C++, no Arduino dependency.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if defined(ARDUINO)
    #include <Arduino.h>
#endif

namespace Lightnet {
    static const uint8_t ENTRY_ID_LEN = 8;
    static const uint8_t ENTRY_ID_MAX = 10;

    inline uint32_t fnv1a32(const char *s)
    {
        uint32_t h = 2166136261u;

        while (s && *s) {
            h ^= (uint8_t)*s++;
            h *= 16777619u;
        }

        return h;
    }

    inline void encodeBase36Lower(uint32_t value, char *out, uint8_t len)
    {
        static const char kDigits[] = "0123456789abcdefghijklmnopqrstuvwxyz";

        for (int i = (int)len - 1; i >= 0; i--) {
            out[i] = kDigits[value % 36];
            value /= 36;
        }

        out[len] = '\0';
    }

    // Stable lowercase [a-z0-9] id from a seed string (built-ins, userColors, one-shot).
    inline void deterministicId(const char *seed, char *out, size_t outLen)
    {
        if (!out || outLen < ENTRY_ID_LEN + 1) {
            if (out && outLen > 0) out[0] = '\0';

            return;
        }

        encodeBase36Lower(fnv1a32(seed), out, ENTRY_ID_LEN);
    }

    inline bool isValidId(const char *id)
    {
        if (!id) return false;

        size_t len = strlen(id);

        if (len < ENTRY_ID_LEN || len > ENTRY_ID_MAX) return false;

        for (const char *c = id; *c; c++) {
            if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9'))) return false;
        }

        return true;
    }

    inline const char *userColorsId()
    {
        static char id[ENTRY_ID_LEN + 1] = { 0 };
        static bool ready = false;

        if (!ready) {
            deterministicId("builtin:userColors", id, sizeof(id));
            ready = true;
        }

        return id;
    }

    inline const char *oneShotId()
    {
        static char id[ENTRY_ID_LEN + 1] = { 0 };
        static bool ready = false;

        if (!ready) {
            deterministicId("internal:one-shot", id, sizeof(id));
            ready = true;
        }

        return id;
    }

    #if defined(ARDUINO)
        inline bool generateRandomId(char *out, size_t outLen, bool (*existsFn)(const char *, void *), void *ctx)
        {
            if (!out || outLen < ENTRY_ID_LEN + 1) return false;

            static const char kAlphabet[] = "0123456789abcdefghijklmnopqrstuvwxyz";

            for (uint8_t attempt = 0; attempt < 32; attempt++) {
                for (uint8_t i = 0; i < ENTRY_ID_LEN; i++) {
                    out[i] = kAlphabet[random(36)];
                }

                out[ENTRY_ID_LEN] = '\0';

                if (!existsFn || !existsFn(out, ctx)) return true;
            }

            out[0] = '\0';

            return false;
        }

    #else
        inline bool generateRandomId(char *out, size_t outLen, bool (*existsFn)(const char *, void *), void *ctx)
        {
            if (!out || outLen < ENTRY_ID_LEN + 1) return false;

            static const char kAlphabet[] = "0123456789abcdefghijklmnopqrstuvwxyz";
            static uint32_t state = 0xA5A5A5A5u;

            for (uint8_t attempt = 0; attempt < 32; attempt++) {
                for (uint8_t i = 0; i < ENTRY_ID_LEN; i++) {
                    state = state * 1664525u + 1013904223u;
                    out[i] = kAlphabet[state % 36];
                }

                out[ENTRY_ID_LEN] = '\0';

                if (!existsFn || !existsFn(out, ctx)) return true;
            }

            out[0] = '\0';

            return false;
        }

    #endif
}  // namespace Lightnet
