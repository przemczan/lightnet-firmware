#pragma once

#include <stdint.h>
#include "../../Core/Common/Palette.hpp"
#include "../../Core/Common/LightnetConfig.hpp"
#include "../../Common/Protocol.hpp"
#include "../../Core/Controller/AnimationScheduler.hpp"
#include "../Palettes/PaletteRepository.hpp"
#include "../../Utils/DeferredWriter.hpp"

namespace Lightnet {
    class AppearanceStore
    {
        public:
            AppearanceStore(AnimationScheduler& scheduler, const PaletteRepository& palettes);

            void loadAndApply();
            void reapply();
            void tick(uint32_t now);
            void flush();

            bool setBrightness(uint8_t value);
            bool setBaseColor(uint8_t slot, Protocol::ColorRGB color);
            bool setAllBaseColors(const Protocol::ColorRGB colors[BASE_COLORS_COUNT]);
            bool setPalette(const char *name);

            uint8_t brightness() const
            {
                return brightnessValue;
            }

            const char * paletteName() const
            {
                return paletteValue;
            }

            Protocol::ColorRGB baseColor(uint8_t slot) const;

            const Protocol::ColorRGB * baseColors() const
            {
                return baseColorsValue;
            }

        private:
            AnimationScheduler& scheduler;
            const PaletteRepository& palettes;

            uint8_t brightnessValue;
            Protocol::ColorRGB baseColorsValue[BASE_COLORS_COUNT];
            char paletteValue[MAX_PALETTE_NAME_LENGTH + 1];

            bool readFile();
            void writeFile();
            void writeDefaults();

            DeferredWriter writer{ 10000 };

            void broadcastSelectedPalette();
    };
}  // namespace Lightnet
