#pragma once

#include <stdint.h>

namespace Lightnet {
    // 4-byte color reference sent over the wire and stored in scene structs.
    // The kind byte selects which union member is meaningful.
    //
    // kind=0 (inline RGB)    : rgb.r, rgb.g, rgb.b
    // kind=1 (palette pos)   : palette.pos    (0..255 sample position)
    // kind=2 (base color)    : useColor.slot  (0..BASE_COLORS_COUNT-1)
    //
    // The `rgb` member is named instead of `inline` because `inline` is a C++ keyword.
    // `raw[3]` overlays the payload bytes for memcpy / wire serialization.
    struct __attribute__((__packed__)) ColorRef {
        uint8_t kind;

        union {
            struct __attribute__((__packed__)) {
                uint8_t r, g, b;
            }     rgb;
            struct __attribute__((__packed__)) {
                uint8_t pos, _0, _1;
            } palette;
            struct __attribute__((__packed__)) {
                uint8_t slot, _0, _1;
            } useColor;

            uint8_t raw[3];
        };
    };

    enum ColorRefKind : uint8_t {
        COLORREF_RGB       = 0,
        COLORREF_PALETTE   = 1,
        COLORREF_USE_COLOR = 2,
    };

    static inline ColorRef ColorRef_rgb(uint8_t r, uint8_t g, uint8_t b)
    {
        ColorRef c;

        c.kind = COLORREF_RGB;
        c.rgb.r = r;
        c.rgb.g = g;
        c.rgb.b = b;

        return c;
    }

    static inline ColorRef ColorRef_palette(uint8_t pos)
    {
        ColorRef c;

        c.kind = COLORREF_PALETTE;
        c.palette.pos = pos;
        c.palette._0 = 0;
        c.palette._1 = 0;

        return c;
    }

    static inline ColorRef ColorRef_useColor(uint8_t slot)
    {
        ColorRef c;

        c.kind = COLORREF_USE_COLOR;
        c.useColor.slot = slot;
        c.useColor._0 = 0;
        c.useColor._1 = 0;

        return c;
    }
}  // namespace Lightnet
