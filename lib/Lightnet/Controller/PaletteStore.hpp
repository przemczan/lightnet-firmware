#pragma once

#include <stdint.h>
#include "../Common/Palette.hpp"
#include "../Common/LightnetConfig.hpp"
#include "../Common/Protocol.hpp"

namespace Lightnet {

// Owner of all palette definitions. v1 ships only built-in palettes compiled into
// flash; user palette CRUD via SPIFFS is a future addition (see animation-system-plan.md).
//
// Special palette names handled outside this store:
//   "userColors" — synthesized from base colors at use time; not stored here.
//   "inline"    — handled by ColorRef{kind=0}; not stored here.
class PaletteStore {
public:
    PaletteStore();

    // Look up a palette by name. Fills outStops with up to PALETTE_STOPS entries.
    // Returns true on success, false if no built-in palette matches.
    // Note: "userColors" is intentionally NOT handled here — the caller (typically
    // AppearanceStore) must synthesize it from the active base colors.
    bool resolve(const char* name, GradientStop* outStops, uint8_t& outCount) const;

    // True if `name` references a known built-in palette.
    bool exists(const char* name) const;

    // Synthesize the "userColors" palette from a set of base colors.
    // 3 stops: (0, c[0]), (128, c[1]), (255, c[2]).
    static void buildUserColors(const Protocol::ColorRGB baseColors[BASE_COLORS_COUNT],
                                GradientStop* outStops, uint8_t& outCount);

    // Number of built-in palettes.
    uint8_t builtInCount() const;
    // Name of the i-th built-in (0..builtInCount()-1), nullptr if out of range.
    const char* builtInName(uint8_t i) const;
};

}  // namespace Lightnet
