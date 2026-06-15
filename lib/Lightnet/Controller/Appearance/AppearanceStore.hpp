#pragma once

#include <stdint.h>
#include "../../Core/Common/Palette.hpp"
#include "../../Core/Common/LightnetConfig.hpp"
#include "../../Common/Protocol.hpp"
#include "../../Core/Controller/AnimationScheduler.hpp"
#include "../Palettes/PaletteStore.hpp"
#include "../../Utils/DeferredWriter.hpp"

namespace Lightnet {
    // Owns `/config/appearance.json` — the persistent record of how the lights currently
    // look (global brightness, base colors, selected palette). On boot, load+apply broadcasts
    // the values to all panels.
    //
    // Writes are deferred: mutators update in-memory state and broadcast to panels immediately,
    // but the filesystem is only written after APPEARANCE_FLUSH_INTERVAL_MS has elapsed since the
    // first un-persisted change. Call tick() from the main loop to drive this. Call flush()
    // before any graceful reboot to guarantee the latest state is saved.
    //
    // the filesystem layout:
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
    class AppearanceStore
    {
        public:
            AppearanceStore(AnimationScheduler& scheduler, const PaletteStore& palettes);

            // Read the file (or create defaults) and broadcast the resolved state to panels.
            // Call once after panel discovery completes and before the WiFi captive portal
            // can block.
            void loadAndApply();

            // Re-broadcast current in-memory state to panels without touching the filesystem.
            // Call after restoring global power to resync panel state.
            void reapply();

            // Call from the main loop. Flushes to the filesystem when the deferred-write interval
            // has elapsed since the first un-persisted change.
            void tick(uint32_t now);

            // Write immediately if dirty. Call before any graceful reboot.
            void flush();

            // Mutators — each one updates in-memory state and broadcasts to panels.
            // the filesystem is written lazily via tick(). Returns false on validation failure (e.g.
            // PATCH /api/appearance with an unknown palette name).
            bool setBrightness(uint8_t value);
            bool setBaseColor(uint8_t slot, Protocol::ColorRGB color);
            bool setAllBaseColors(const Protocol::ColorRGB colors[BASE_COLORS_COUNT]);
            bool setPalette(const char *name);

            // Read-only accessors.
            uint8_t              brightness() const
            {
                return brightnessValue;
            }

            const char *          paletteName() const
            {
                return paletteValue;
            }

            Protocol::ColorRGB   baseColor(uint8_t slot) const;

            const Protocol::ColorRGB * baseColors() const
            {
                return baseColorsValue;
            }

        private:
            AnimationScheduler& scheduler;
            const PaletteStore& palettes;

            uint8_t brightnessValue;
            Protocol::ColorRGB baseColorsValue[BASE_COLORS_COUNT];
            char paletteValue[20];         // 19 chars max + null

            // Persistence helpers
            bool readFile();      // returns true if a valid file existed
            void writeFile();     // atomic tmp+rename
            void writeDefaults(); // populate in-memory state with defaults

            DeferredWriter writer{ 10000 };

            // Resolve and broadcast the currently selected palette (handling the
            // synthetic "userColors" case).
            void broadcastSelectedPalette();
    };
}  // namespace Lightnet
