#pragma once

#include <stdint.h>
#include "../../Utils/Debug.hpp"
#include "../../Core/Common/Palette.hpp"
#include "../../Core/Common/LightnetConfig.hpp"
#include "../../Core/Controller/IPaletteResolver.hpp"
#include "../../Common/Protocol.hpp"

namespace Lightnet {
    // Owner of all palette definitions. Built-in palettes live in flash. User-defined
    // palettes are stored in the filesystem at /palettes/<name>.json.
    //
    // Special palette names handled outside this store:
    //   "userColors" — synthesized from base colors at use time; not stored here.
    class PaletteStore : public IPaletteResolver
    {
        public:
            PaletteStore();

            // Look up a palette by name. Checks built-ins first, then the filesystem.
            // Fills outStops with up to PALETTE_STOPS entries. Returns true on success.
            // "userColors" is intentionally NOT handled here — callers must synthesize it.
            bool resolve(const char *name, GradientStop *outStops, uint8_t& outCount) const override;

            // True if `name` is a known palette (built-in or user-defined on the filesystem).
            bool exists(const char *name) const;

            // True if `name` is one of the compiled-in palettes.
            bool isBuiltIn(const char *name) const;

            // Save a user palette to the filesystem (/palettes/<name>.json). Overwrites if exists.
            // Returns false if the filesystem write fails or name is invalid.
            bool save(const char *name, const GradientStop *stops, uint8_t count) const;

            // Delete a user palette from the filesystem. Returns false if it's a built-in (caller
            // should respond 403) or the file does not exist (caller should respond 404).
            bool deleteUserPalette(const char *name) const;

            // Synthesize the "userColors" palette from a set of base colors.
            static void buildUserColors(
                const Protocol::ColorRGB baseColors[BASE_COLORS_COUNT],
                GradientStop *           outStops,
                uint8_t&                 outCount
            );

            // Number of compiled-in palettes.
            uint8_t builtInCount() const;
            // Name of the i-th built-in (0..builtInCount()-1), nullptr if out of range.
            const char * builtInName(uint8_t i) const;

            // Palette JSON parsing lives in PaletteJson.hpp as a free function
            // (Lightnet::parsePaletteJson) — no the filesystem dependency so it can be
            // exercised by native unit tests.
    };
}  // namespace Lightnet
