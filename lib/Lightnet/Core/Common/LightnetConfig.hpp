#pragma once

#include <stdint.h>

namespace Lightnet {
    // System-wide panel count cap. Referenced by AnimationScheduler,
    // and ScenePlayer panel targeting.
    //
    // ESP8266 has ~80 KB DRAM shared between the WiFi stack, heap, and BSS. Fixed-size arrays
    // indexed by panel count (PanelGraph, TopologyIndex, PanelGeometry, AnimationScheduler
    // panelStates) all live in heap via ScenePlayer/AnimationScheduler; the topology float
    // arrays in PanelGeometry alone are 7 × N × 4 bytes. 32 is the practical ceiling for
    // ESP8266: it saves ~3.3 KB heap (PanelGeometry + PanelGraph + TopologyIndex + panelStates)
    // and ~1 KB on the 4096-byte continuation stack (rebuild() + fireStep() local arrays),
    // preventing both OOM during concurrent HTTP/WS operation and stack overflow during scene load.
    // ESP32 and native unit tests use the full 100.
    #if defined(ARDUINO_ARCH_ESP8266)
        static const uint8_t LIGHTNET_MAX_PANELS = 32;

    #else
        static const uint8_t LIGHTNET_MAX_PANELS = 100;

    #endif

    // Gradient stops per palette (WLED-compatible). Each stop is 4 bytes (pos + RGB).
    static const uint8_t PALETTE_STOPS = 16;

    // Base color slots per scene/appearance: primary, secondary, tertiary.
    static const uint8_t BASE_COLORS_COUNT = 3;
}  // namespace Lightnet
