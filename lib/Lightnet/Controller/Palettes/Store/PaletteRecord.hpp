#pragma once

#include "../../../Core/Common/LightnetConfig.hpp"
#include "../../../Core/Common/Palette.hpp"
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>

namespace Lightnet {
    static const uint8_t MAX_PALETTE_NAME_LENGTH = 30;

    struct PaletteRecord {
        char         name[MAX_PALETTE_NAME_LENGTH + 1];
        bool         builtin;
        uint8_t      stopsCount;
        GradientStop stops[PALETTE_STOPS];
    } __attribute__((packed));

    inline bool isValidPaletteName(const char *name)
    {
        if (!name || name[0] == '\0') return false;

        return strlen(name) <= MAX_PALETTE_NAME_LENGTH;
    }

    inline bool paletteNamesEqual(const char *a, const char *b)
    {
        if (!a || !b) return false;

        return strcasecmp(a, b) == 0;
    }

    static_assert(sizeof(GradientStop) == 4, "GradientStop wire size");
}  // namespace Lightnet
