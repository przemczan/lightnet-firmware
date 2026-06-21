#pragma once

#include <stdint.h>
#include "../../Core/Common/LightnetConfig.hpp"
#include "../../Common/Protocol.hpp"
#include "../../Utils/DeferredWriter.hpp"
#include "../../Common/Database/SingleRecordStore.hpp"
#include "Store/AppearanceCodec.hpp"

namespace Lightnet {
    // Storage-only owner of the appearance settings record (`/config/appearance.db`).
    // Holds no scheduler or palette repository — broadcasting and palette resolution
    // live in AppearanceService. Setters mutate state and mark dirty; persistence is
    // deferred and flushed via tick()/flush().
    class AppearanceStore
    {
        public:
            AppearanceStore();

            // Loads the stored record, writing defaults if none exists yet.
            void load();
            void tick(uint32_t now);
            void flush();

            void setBrightness(uint8_t value);
            bool setBaseColor(uint8_t slot, Protocol::ColorRGB color);
            void setAllBaseColors(const Protocol::ColorRGB colors[BASE_COLORS_COUNT]);
            void setPalette(const char *name);

            uint8_t brightness() const
            {
                return _record.brightness;
            }

            const char * paletteName() const
            {
                return _record.palette;
            }

            Protocol::ColorRGB baseColor(uint8_t slot) const;

            const Protocol::ColorRGB * baseColors() const
            {
                return _record.baseColors;
            }

        private:
            static constexpr const char *APPEARANCE_DATABASE_PATH = "/config/appearance.db";
            static constexpr const char *APPEARANCE_DATA_DIR      = "/config";

            AppearanceRecord _record;

            void writeFile();
            void writeDefaults();

            DeferredWriter writer{ 10000 };

            SingleRecordStore<AppearanceCodec> _store{
                APPEARANCE_DATABASE_PATH, APPEARANCE_DATA_DIR
            };
    };
}  // namespace Lightnet
