#pragma once

#include <stdint.h>
#include "../../Utils/DeferredWriter.hpp"

namespace Lightnet {
    constexpr uint8_t POWER_ALWAYS_ON  = 0;
    constexpr uint8_t POWER_ALWAYS_OFF = 1;
    constexpr uint8_t POWER_LAST_STATE = 2;

    // Owns `/config/configuration.json` — persistent app-level settings.
    //
    // SPIFFS layout:
    //   /config/configuration.json
    //   { "schemaVersion": 1, "powerStateOnBoot": 0 }
    //
    // powerStateOnBoot controls which isOn value AppStateStore applies on boot:
    //   0 = POWER_ALWAYS_ON  — always start with isOn = true
    //   1 = POWER_ALWAYS_OFF — always start with isOn = false
    //   2 = POWER_LAST_STATE — restore the last persisted isOn value
    class ConfigurationStore
    {
        public:
            ConfigurationStore();

            void load();
            void tick(uint32_t now);
            void flush();

            uint8_t powerStateOnBoot() const
            {
                return _powerStateOnBoot;
            }

            // Returns false if value is out of range (not 0-2).
            bool setPowerStateOnBoot(uint8_t v);

        private:
            uint8_t _powerStateOnBoot = POWER_ALWAYS_ON;
            DeferredWriter writer{ 5000 };

            bool readFile();
            void writeFile();
    };
}  // namespace Lightnet
