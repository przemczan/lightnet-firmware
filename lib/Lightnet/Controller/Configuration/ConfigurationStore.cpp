#include <Arduino.h>
#include "ConfigurationStore.hpp"
#include "../../Utils/SimpleJson.hpp"
#include "../../Utils/Debug.hpp"
#include <FS.h>
#ifdef ARDUINO_ARCH_ESP32
    #include <SPIFFS.h>
#endif
#include <string.h>

namespace Lightnet {
    namespace {
        const char *CONFIG_PATH     = "/config/configuration.json";
        const char *CONFIG_TMP_PATH = "/config/configuration.json.tmp";
        const uint8_t CONFIG_SCHEMA = 1;
    } // anonymous namespace

    ConfigurationStore::ConfigurationStore()
        : writer(5000)
    {
    }

    void ConfigurationStore::load()
    {
        if (!readFile()) {
            D_PRINTLN("[CONFIG] no valid file; writing defaults");
            writeFile();
        }
    }

    bool ConfigurationStore::setPowerStateOnBoot(uint8_t v)
    {
        if (v > POWER_LAST_STATE) return false;

        if (v == _powerStateOnBoot) return true;

        _powerStateOnBoot = v;
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

    bool ConfigurationStore::readFile()
    {
        if (!SPIFFS.exists(CONFIG_PATH)) return false;

        File f = SPIFFS.open(CONFIG_PATH, "r");

        if (!f) return false;

        char buf[128];
        size_t n = f.readBytes(buf, sizeof(buf) - 1);

        f.close();
        buf[n] = '\0';

        SimpleJson j(buf, n);

        long schema = j.getInt("schemaVersion");

        if (schema > 0 && schema != CONFIG_SCHEMA) {
            D_PRINTLN("[CONFIG] schema mismatch — using defaults");

            return false;
        }

        long psob = j.getInt("powerStateOnBoot");

        if (psob >= 0 && psob <= POWER_LAST_STATE) {
            _powerStateOnBoot = (uint8_t)psob;
        }

        return true;
    }

    void ConfigurationStore::writeFile()
    {
        File f = SPIFFS.open(CONFIG_TMP_PATH, "w");

        if (!f) {
            D_PRINTLN("[CONFIG] failed to open tmp file");

            return;
        }

        char buf[64];
        int len = snprintf(buf, sizeof(buf),
                           "{\"schemaVersion\":%u,\"powerStateOnBoot\":%u}\n",
                           (unsigned)CONFIG_SCHEMA,
                           (unsigned)_powerStateOnBoot);

        if (len > 0) f.write((const uint8_t *)buf, (size_t)len);

        f.close();

        SPIFFS.remove(CONFIG_PATH);
        SPIFFS.rename(CONFIG_TMP_PATH, CONFIG_PATH);
    }
}  // namespace Lightnet
