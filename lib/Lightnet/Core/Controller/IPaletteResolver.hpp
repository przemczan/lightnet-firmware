#pragma once

// IPaletteResolver — named-palette lookup seam for the shared scene engine.
//
// ScenePlayer resolves a scene/layer palette id into gradient stops through this
// interface. The controller impl is LittleFsPaletteRepository; the mobile impl is an
// in-memory map supplied by the app. The userColors palette is synthesized by the
// caller via Core/Common/UserColors.hpp, not here.

#include <stdint.h>
#include "../Common/Palette.hpp"  // Lightnet::GradientStop

namespace Lightnet {
    class IPaletteResolver
    {
        public:
            virtual ~IPaletteResolver()
            {
            }

            // Fill outStops with up to PALETTE_STOPS entries for the palette id.
            // Returns true on success, false if the name is unknown.
            virtual bool resolve(const char *name, GradientStop *outStops, uint8_t &outCount) const = 0;
    };
}  // namespace Lightnet
