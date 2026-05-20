#pragma once

#include <stdint.h>
#include "../Common/Palette.hpp"
#include "../Common/LightnetConfig.hpp"
#include "../Common/Protocol.hpp"
#include "AnimationScheduler.hpp"
#include "PaletteStore.hpp"

namespace Lightnet {

// Owns `/config/appearance.json` — the persistent record of how the lights currently
// look (global brightness, base colors, selected palette). On boot, load+apply broadcasts
// the values to all panels. Mutators atomically rewrite the file and broadcast to panels.
//
// SPIFFS layout:
//   /config/appearance.json
//   {
//     "schemaVersion": 1,
//     "brightness": 192,
//     "baseColors": ["#FF4400", "#FF8800", "#000000"],
//     "palette": "lava"
//   }
//
// The file is written via tmp+rename so a power loss during save never corrupts the
// previous good copy.
class AppearanceStore {
public:
    AppearanceStore(AnimationScheduler& scheduler, const PaletteStore& palettes);

    // Read the file (or create defaults) and broadcast the resolved state to panels.
    // Call once after panel discovery completes and before the WiFi captive portal
    // can block.
    void loadAndApply();

    // Mutators — each one updates in-memory state, persists to SPIFFS, and broadcasts
    // the relevant packet to the panels. Returns false on validation failure (e.g.
    // PUT /api/palette with an unknown palette name).
    bool setBrightness(uint8_t value);
    bool setBaseColor(uint8_t slot, Protocol::ColorRGB color);
    bool setAllBaseColors(const Protocol::ColorRGB colors[BASE_COLORS_COUNT]);
    bool setPalette(const char* name);

    // Read-only accessors.
    uint8_t              brightness() const { return brightnessValue; }
    const char*          paletteName() const { return paletteValue; }
    Protocol::ColorRGB   baseColor(uint8_t slot) const;
    const Protocol::ColorRGB* baseColors() const { return baseColorsValue; }

private:
    AnimationScheduler& scheduler;
    const PaletteStore& palettes;

    uint8_t            brightnessValue;
    Protocol::ColorRGB baseColorsValue[BASE_COLORS_COUNT];
    char               paletteValue[20];   // 19 chars max + null

    // Persistence helpers
    bool readFile();         // returns true if a valid file existed
    void writeFile();        // atomic tmp+rename
    void writeDefaults();    // populate in-memory state with defaults

    // Resolve and broadcast the currently selected palette (handling the
    // synthetic "userColors" case).
    void broadcastSelectedPalette();
};

}  // namespace Lightnet
