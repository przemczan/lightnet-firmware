#pragma once

#include <stdint.h>
#include "AppearanceStore.hpp"
#include "../../Core/Common/Palette.hpp"
#include "../../Core/Common/LightnetConfig.hpp"
#include "../../Common/Protocol.hpp"
#include "../../Core/Controller/AnimationScheduler.hpp"
#include "../Palettes/PaletteRepository.hpp"

namespace Lightnet {
    // Behaviour facade over AppearanceStore: applies appearance settings to the panels
    // through AnimationScheduler and resolves the selected palette via PaletteRepository.
    // Owns the storage; the HTTP layer and main loop talk to this service, never the
    // store directly. Getters delegate to the store; mutators persist via the store and
    // broadcast the change to panels.
    class AppearanceService
    {
        public:
            AppearanceService(AnimationScheduler& scheduler, const PaletteRepository& palettes);

            // Loads persisted settings (defaults if none) and broadcasts them to panels.
            void loadAndApply();
            // Re-broadcasts the current settings (e.g. on power-on).
            void reapply();
            void tick(uint32_t now);
            void flush();

            bool setBrightness(uint8_t value);
            bool setBaseColor(uint8_t slot, Protocol::ColorRGB color);
            bool setAllBaseColors(const Protocol::ColorRGB colors[BASE_COLORS_COUNT]);
            bool setPalette(const char *name);

            uint8_t brightness() const
            {
                return store.brightness();
            }

            const char * paletteName() const
            {
                return store.paletteName();
            }

            Protocol::ColorRGB baseColor(uint8_t slot) const
            {
                return store.baseColor(slot);
            }

            const Protocol::ColorRGB * baseColors() const
            {
                return store.baseColors();
            }

        private:
            AppearanceStore store;
            AnimationScheduler& scheduler;
            const PaletteRepository& palettes;

            void broadcastSelectedPalette();
    };
}  // namespace Lightnet
