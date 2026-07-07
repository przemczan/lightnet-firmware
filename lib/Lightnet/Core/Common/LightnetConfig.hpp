#pragma once

#include <stdint.h>

namespace Lightnet {
    // System-wide panel count cap. Referenced by AnimationScheduler, and ScenePlayer panel
    // targeting. Every controller target is ESP32-class (see platformio.ini), with DRAM to spare
    // for the full 100.
    static const uint8_t LIGHTNET_MAX_PANELS = 100;

    // Gradient stops per palette (WLED-compatible). Each stop is 4 bytes (pos + RGB).
    static const uint8_t PALETTE_STOPS = 16;

    // Base color slots per scene/appearance: primary, secondary, tertiary.
    static const uint8_t BASE_COLORS_COUNT = 3;
}  // namespace Lightnet
