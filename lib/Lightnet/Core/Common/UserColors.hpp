#pragma once

// UserColors — synthesize the special "userColors" palette from a scene's base
// colors. Pure (no Arduino/FS) so the shared scene engine can build it directly,
// instead of reaching into the filesystem-backed PaletteStore. PaletteStore keeps a
// thin static delegate for controller call sites.

#include <stdint.h>
#include "ProtocolTypes.hpp"   // Protocol::ColorRGB
#include "Palette.hpp"         // Lightnet::GradientStop
#include "LightnetConfig.hpp"  // BASE_COLORS_COUNT

namespace Lightnet {
    // 3-stop gradient (primary @0, secondary @128, tertiary @255) from the base colors.
    inline void buildUserColors(
        const Protocol::ColorRGB baseColors[BASE_COLORS_COUNT],
        GradientStop *           outStops,
        uint8_t&                 outCount
    )
    {
        outStops[0] = { 0,   baseColors[0].r, baseColors[0].g, baseColors[0].b };
        outStops[1] = { 128, baseColors[1].r, baseColors[1].g, baseColors[1].b };
        outStops[2] = { 255, baseColors[2].r, baseColors[2].g, baseColors[2].b };
        outCount = 3;
    }
}  // namespace Lightnet
