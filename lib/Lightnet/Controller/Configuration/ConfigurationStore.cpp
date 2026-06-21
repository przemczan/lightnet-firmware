#include <Arduino.h>
#include "ConfigurationStore.hpp"
#include "../../Utils/Debug.hpp"

namespace Lightnet {
    ConfigurationStore::ConfigurationStore()
        : writer(5000)
    {
    }

    void ConfigurationStore::load()
    {
        if (!_store.load(_record)) {
            D_PRINTLN("[CONFIG] no valid record; writing defaults");
            _record.powerStateOnBoot = POWER_ALWAYS_ON;
            writeFile();
        }
    }

    bool ConfigurationStore::setPowerStateOnBoot(uint8_t v)
    {
        if (v > POWER_LAST_STATE) return false;

        if (v == _record.powerStateOnBoot) return true;

        _record.powerStateOnBoot = v;
        writer.markDirty(millis());

        return true;
    }

    void ConfigurationStore::tick(uint32_t now)
    {
        if (writer.shouldFlush(now)) {
            writeFile();
            writer.clear();
        }
    }

    void ConfigurationStore::flush()
    {
        if (writer.isDirty()) {
            writeFile();
            writer.clear();
        }
    }

    void ConfigurationStore::writeFile()
    {
        if (!_store.save(_record)) {
            D_PRINTLN("[CONFIG] failed to persist record");
        }
    }
}  // namespace Lightnet
