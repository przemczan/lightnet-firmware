#pragma once

#include <stdint.h>
#include "../../Utils/DeferredWriter.hpp"
#include "../../Common/Database/SingleRecordStore.hpp"
#include "Store/ConfigurationCodec.hpp"

namespace Lightnet {
    // Owns `/config/configuration.db` — persistent app-level settings, stored as a single
    // binary Database record (see ConfigurationRecord for the power-state semantics).
    class ConfigurationStore
    {
        public:
            ConfigurationStore();

            void load();
            void tick(uint32_t now);
            void flush();

            uint8_t powerStateOnBoot() const
            {
                return _record.powerStateOnBoot;
            }

            // Returns false if value is out of range (not 0-2).
            bool setPowerStateOnBoot(uint8_t v);

        private:
            static constexpr const char *CONFIGURATION_DATABASE_PATH = "/config/configuration.db";
            static constexpr const char *CONFIGURATION_DATA_DIR      = "/config";

            ConfigurationRecord _record{ POWER_ALWAYS_ON };
            DeferredWriter      writer{ 5000 };

            SingleRecordStore<ConfigurationCodec> _store{
                CONFIGURATION_DATABASE_PATH, CONFIGURATION_DATA_DIR
            };

            void writeFile();
    };
}  // namespace Lightnet
