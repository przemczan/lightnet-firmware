#pragma once

#include <stdint.h>
#include "LightnetConfig.hpp"

namespace Lightnet {
// Single palette gradient stop: position (0..255) + RGB color.
// 4 bytes per stop, PALETTE_STOPS stops per palette = 64 bytes total.
// Intentionally does not depend on Protocol::ColorRGB so this header can be
// included by Protocol.hpp without a circular dependency.
    struct __attribute__((__packed__)) GradientStop {
        uint8_t pos;
        uint8_t r;
        uint8_t g;
        uint8_t b;
    };

// Sample a palette at position 0..255. Linear interpolation between adjacent stops.
// Writes the resulting (r, g, b) triple into out[0..2].
// `stops` must contain `count` valid entries with strictly increasing positions.
// Caller guarantees count >= 1 (defaults to white if violated).
    static inline void samplePalette(
        const GradientStop *stops,
        uint8_t             count,
        uint8_t             pos,
        uint8_t *           out_r,
        uint8_t *           out_g,
        uint8_t *           out_b
    )
    {
        if (count == 0) {
            *out_r = 0xFF;
            *out_g = 0xFF;
            *out_b = 0xFF;

            return;
        }

        if (count == 1 || pos <= stops[0].pos) {
            *out_r = stops[0].r;
            *out_g = stops[0].g;
            *out_b = stops[0].b;

            return;
        }

        if (pos >= stops[count - 1].pos) {
            const GradientStop& s = stops[count - 1];

            *out_r = s.r;
            *out_g = s.g;
            *out_b = s.b;

            return;
        }

        uint8_t i = 0;

        while (i + 1 < count && stops[i + 1].pos <= pos) {
            i++;
        }

        const GradientStop& a = stops[i];
        const GradientStop& b = stops[i + 1];

        uint8_t span = (uint8_t)(b.pos - a.pos);

        if (span == 0) {
            *out_r = a.r;
            *out_g = a.g;
            *out_b = a.b;

            return;
        }

        uint8_t frac_q8 = (uint8_t)(((uint16_t)(pos - a.pos) * 255) / span);

        *out_r = (uint8_t)(a.r + (((int16_t)b.r - (int16_t)a.r) * frac_q8) / 255);
        *out_g = (uint8_t)(a.g + (((int16_t)b.g - (int16_t)a.g) * frac_q8) / 255);
        *out_b = (uint8_t)(a.b + (((int16_t)b.b - (int16_t)a.b) * frac_q8) / 255);
    }
}  // namespace Lightnet
