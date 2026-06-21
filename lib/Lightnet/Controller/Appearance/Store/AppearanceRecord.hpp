#pragma once

#include <stdint.h>
#include "../../../Core/Common/ProtocolTypes.hpp"
#include "../../../Core/Common/LightnetConfig.hpp"
#include "../../Palettes/Store/PaletteRecord.hpp"

namespace Lightnet {
    // Persistent appearance settings (one record per controller): global brightness, the
    // three user base colours, and the selected palette name.
    struct AppearanceRecord {
        uint8_t            brightness;
        Protocol::ColorRGB baseColors[BASE_COLORS_COUNT];
        char               palette[MAX_PALETTE_NAME_LENGTH + 1];
    } __attribute__((packed));
}  // namespace Lightnet
