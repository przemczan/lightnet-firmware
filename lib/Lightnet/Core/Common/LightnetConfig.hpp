#pragma once

#include <stdint.h>

namespace Lightnet {
    // System-wide panel count cap. Referenced by AnimationScheduler, and ScenePlayer panel
    // targeting. Every controller target is ESP32-class (see platformio.ini), with DRAM to spare
    // for the full 100.
    static const uint8_t LIGHTNET_MAX_PANELS = 100;

    // Panel address/index for everything above the wire format. Valid values are
    // 1..LIGHTNET_MAX_PANELS, with 0 meaning broadcast/general-call, so a byte holds any of
    // them. The wire protocol's PacketHeader.targetPanelIndex and the discovery structs keep
    // their uint16_t fields for layout stability — reading one of those into a PanelIndex is
    // the single narrowing point.
    typedef uint8_t PanelIndex;

    // Gradient stops per palette (WLED-compatible). Each stop is 4 bytes (pos + RGB).
    static const uint8_t PALETTE_STOPS = 16;

    // Base color slots per scene/appearance: primary, secondary, tertiary.
    static const uint8_t BASE_COLORS_COUNT = 3;
}  // namespace Lightnet
