#pragma once

#include <stdint.h>

// ============================================================================
// ColorCompose — pure integer colour blending + modifier math for the panel
// layer compositor. No Arduino / FastLED dependency, so it is unit-testable
// natively and can be mirrored bit-for-bit in the Kotlin PanelAnimationPlayer
// port and the anim-refgen reference generator.
//
// A panel drives a single RGB output, so "compositing" is folding a handful of
// RGB triples per tick: SOURCE layers combine via a ComposeMode, MODIFIER layers
// transform the colour accumulated below them (brightness in RGB, saturation /
// hue in integer HSV). The accumulator starts black.
//
// Integer HSV (H/S/V all 0..255) is the classic 6-sector conversion. The
// round-trip is approximate (documented, accepted) but fully deterministic.
// ============================================================================

namespace Lightnet {
    struct RGB8 {
        uint8_t r;
        uint8_t g;
        uint8_t b;
    };

    // ---- Blend modes for SOURCE layers (matches ComposeMode in AnimationTypes.hpp) ----
    enum ComposeOp : uint8_t {
        CO_OPAQUE   = 0,  // opaque: top wins (default — reproduces legacy last-write)
        CO_ADD      = 1,  // additive, clamped
        CO_MAX      = 2,  // per-channel lighten
        CO_MULTIPLY = 3,  // darken / mask
        CO_SCREEN   = 4,  // soft lighten
        CO_DARKEN     = 6,  // per-channel darken (min)
        CO_OVERLAY    = 7,  // multiply shadows, screen highlights (contrast boost)
        CO_DIFFERENCE = 8,  // per-channel absolute difference
        CO_SUBTRACT   = 9,  // acc - src, clamped to 0
    };

    static inline uint8_t cc_clampAdd(uint8_t a, uint8_t b)
    {
        uint16_t s = (uint16_t)a + b;

        return (s > 255) ? 255 : (uint8_t)s;
    }

    static inline uint8_t cc_mul(uint8_t a, uint8_t b)
    {
        return (uint8_t)(((uint16_t)a * b) / 255);
    }

    static inline uint8_t cc_screen(uint8_t a, uint8_t b)
    {
        return (uint8_t)(255 - cc_mul((uint8_t)(255 - a), (uint8_t)(255 - b)));
    }

    static inline uint8_t cc_max(uint8_t a, uint8_t b)
    {
        return (a > b) ? a : b;
    }

    static inline uint8_t cc_min(uint8_t a, uint8_t b)
    {
        return (a < b) ? a : b;
    }

    static inline uint8_t cc_overlay(uint8_t a, uint8_t b)
    {
        return (a < 128)
            ? (uint8_t)((2 * (uint16_t)a * b) / 255)
            : (uint8_t)(255 - (2 * (uint16_t)(255 - a) * (255 - b)) / 255);
    }

    static inline uint8_t cc_diff(uint8_t a, uint8_t b)
    {
        return (a > b) ? (uint8_t)(a - b) : (uint8_t)(b - a);
    }

    static inline uint8_t cc_sub(uint8_t a, uint8_t b)
    {
        return (a > b) ? (uint8_t)(a - b) : 0;
    }

    // Fold a source colour onto the accumulator using the given blend op.
    static inline RGB8 composeColor(RGB8 acc, RGB8 src, uint8_t op)
    {
        switch (op) {
            case CO_ADD:
                return { cc_clampAdd(acc.r, src.r), cc_clampAdd(acc.g, src.g), cc_clampAdd(acc.b, src.b) };
            case CO_MAX:
                return { cc_max(acc.r, src.r), cc_max(acc.g, src.g), cc_max(acc.b, src.b) };
            case CO_MULTIPLY:
                return { cc_mul(acc.r, src.r), cc_mul(acc.g, src.g), cc_mul(acc.b, src.b) };
            case CO_SCREEN:
                return { cc_screen(acc.r, src.r), cc_screen(acc.g, src.g), cc_screen(acc.b, src.b) };
            case CO_DARKEN:
                return { cc_min(acc.r, src.r), cc_min(acc.g, src.g), cc_min(acc.b, src.b) };
            case CO_OVERLAY:
                return { cc_overlay(acc.r, src.r), cc_overlay(acc.g, src.g), cc_overlay(acc.b, src.b) };
            case CO_DIFFERENCE:
                return { cc_diff(acc.r, src.r), cc_diff(acc.g, src.g), cc_diff(acc.b, src.b) };
            case CO_SUBTRACT:
                return { cc_sub(acc.r, src.r), cc_sub(acc.g, src.g), cc_sub(acc.b, src.b) };
            case CO_OPAQUE:
            default:
                return src;
        }
    }

    // ---- Integer HSV (H/S/V 0..255), classic 6-sector ----
    struct HSV8 {
        uint8_t h;
        uint8_t s;
        uint8_t v;
    };

    static inline HSV8 rgb2hsv(RGB8 c)
    {
        uint8_t mx = cc_max(cc_max(c.r, c.g), c.b);
        uint8_t mn = (c.r < c.g) ? ((c.r < c.b) ? c.r : c.b) : ((c.g < c.b) ? c.g : c.b);

        HSV8 out;

        out.v = mx;

        if (mx == 0) {
            out.s = 0;
            out.h = 0;

            return out;
        }

        uint8_t delta = (uint8_t)(mx - mn);

        out.s = (uint8_t)(((uint16_t)delta * 255) / mx);

        if (delta == 0) {
            out.h = 0;

            return out;
        }

        int16_t h;

        if (mx == c.r)      h = (int16_t)(((int16_t)c.g - (int16_t)c.b) * 43 / delta);
        else if (mx == c.g) h = (int16_t)(85 + ((int16_t)c.b - (int16_t)c.r) * 43 / delta);
        else h = (int16_t)(171 + ((int16_t)c.r - (int16_t)c.g) * 43 / delta);

        out.h = (uint8_t)(h & 0xFF); // wraps the red-sector negative case mod 256

        return out;
    }

    static inline RGB8 hsv2rgb(HSV8 c)
    {
        if (c.s == 0) {
            return { c.v, c.v, c.v };
        }

        uint8_t region    = (uint8_t)(c.h / 43);             // 0..5
        uint8_t remainder = (uint8_t)((c.h - region * 43) * 6);  // 0..255

        uint8_t p = (uint8_t)(((uint16_t)c.v * (255 - c.s)) / 255);
        uint8_t q = (uint8_t)(((uint16_t)c.v * (255 - (((uint16_t)c.s * remainder) / 255))) / 255);
        uint8_t t = (uint8_t)(((uint16_t)c.v * (255 - (((uint16_t)c.s * (255 - remainder)) / 255))) / 255);

        switch (region) {
            case 0:  return { c.v, t, p };
            case 1:  return { q, c.v, p };
            case 2:  return { p, c.v, t };
            case 3:  return { p, q, c.v };
            case 4:  return { t, p, c.v };
            default: return { c.v, p, q };
        }
    }

    // ---- Modifier ops: transform the accumulated colour ----
    // `value` is the animated scalar (0..255). Identity points noted per op.

    // Brightness: scale value down. Identity at 255. Pure RGB (lossless).
    static inline RGB8 modBrightness(RGB8 acc, uint8_t value)
    {
        return { cc_mul(acc.r, value), cc_mul(acc.g, value), cc_mul(acc.b, value) };
    }

    // Saturation: scale S. Identity at 255 (value<255 desaturates toward grey).
    // The identity value bypasses the approximate HSV round-trip so it never drifts colour.
    static inline RGB8 modSaturation(RGB8 acc, uint8_t value)
    {
        if (value == 255) return acc;

        HSV8 h = rgb2hsv(acc);

        h.s = cc_mul(h.s, value);

        return hsv2rgb(h);
    }

    // Hue shift: rotate H by `value`. Identity at 0; sweep 0..255 = full rotation.
    // The identity value bypasses the approximate HSV round-trip so it never drifts colour.
    static inline RGB8 modHueShift(RGB8 acc, uint8_t value)
    {
        if (value == 0) return acc;

        HSV8 h = rgb2hsv(acc);

        h.h = (uint8_t)((h.h + value) & 0xFF);

        return hsv2rgb(h);
    }

    // ---- Layer fold (the panel compositor's per-tick contract, made pure) ----
    enum ModOp : uint8_t {
        MO_BRIGHTNESS = 0, MO_SATURATION = 1, MO_HUE = 2
    };

    struct CompositeLayer {
        uint8_t composeOrder; // stacking key (folded ascending)
        bool    isModifier;
        uint8_t op;           // ComposeOp (source) or ModOp (modifier)
        uint8_t value;        // modifier scalar (modifier only)
        RGB8    color;        // source colour (source only)
    };

    // Fold `n` active layer contributions onto the `base` accumulator (the scene
    // background — black pre-v6), in ascending composeOrder. Source layers blend via
    // their ComposeOp; modifier layers transform the accumulated colour. Caller supplies
    // only layers that are actually contributing this frame (delayed/transparent slots
    // are already excluded). `layers` is sorted in place. This is exactly what
    // AnimationPlayer::composite() runs each tick.
    static inline RGB8 foldLayers(CompositeLayer *layers, uint8_t n, RGB8 base)
    {
        // Insertion sort by composeOrder (n is small — at most MAX_ANIM_SLOTS).
        for (uint8_t i = 1; i < n; i++) {
            CompositeLayer tmp = layers[i];
            uint8_t j = i;

            while (j > 0 && layers[j - 1].composeOrder > tmp.composeOrder) {
                layers[j] = layers[j - 1];
                j--;
            }

            layers[j] = tmp;
        }

        RGB8 acc = base;

        for (uint8_t k = 0; k < n; k++) {
            const CompositeLayer& L = layers[k];

            if (L.isModifier) {
                switch (L.op) {
                    case MO_BRIGHTNESS: acc = modBrightness(acc, L.value);
                        break;
                    case MO_SATURATION: acc = modSaturation(acc, L.value);
                        break;
                    case MO_HUE:        acc = modHueShift(acc, L.value);
                        break;
                    default: break;
                }
            } else {
                acc = composeColor(acc, L.color, L.op);
            }
        }

        return acc;
    }
}  // namespace Lightnet
