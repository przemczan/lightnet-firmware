#include <Arduino.h>
#include "AppStateStore.hpp"
#include "../../Utils/SimpleJson.hpp"
#include "../../Utils/Debug.hpp"
#include <FS.h>
#ifdef ARDUINO_ARCH_ESP32
    #include <SPIFFS.h>
#endif
#include <string.h>

namespace Lightnet {
    namespace {
        const char *APP_STATE_PATH     = "/config/app_state.json";
        const char *APP_STATE_TMP_PATH = "/config/app_state.json.tmp";
        const uint8_t APP_STATE_SCHEMA = 1;
    } // anonymous namespace

    AppStateStore::AppStateStore()
        : writer(5000)
    {
    }

    void AppStateStore::load()
    {
        if (!readFile()) {
            D_PRINTLN("[APP_STATE] no valid file; writing defaults");
            writeFile();
        }
    }

    bool AppStateStore::setIsOn(bool value)
    {
        if (value == _isOn) return false;

        _isOn = value;
        writer.markDirty(millis());

        return true;
    }

    void AppStateStore::tick(uint32_t now)
    {
        if (writer.shouldFlush(now)) {
            writeFile();
            writer.clear();
        }
    }

    void AppStateStore::flush()
    {
        if (writer.isDirty()) {
            writeFile();
            writer.clear();
        }
    }

    bool AppStateStore::readFile()
    {
        if (!SPIFFS.exists(APP_STATE_PATH)) return false;

        File f = SPIFFS.open(APP_STATE_PATH, "r");

        if (!f) return false;

        char buf[128];
        size_t n = f.readBytes(buf, sizeof(buf) - 1);

        f.close();
        buf[n] = '\0';

        SimpleJson j(buf, n);

        long schema = j.getInt("schemaVersion");

        if (schema > 0 && schema != APP_STATE_SCHEMA) {
            D_PRINTLN("[APP_STATE] schema mismatch — using defaults");

            return false;
        }

        const char *isOnVal = j.rawValue("isOn");

        if (isOnVal) {
            const char *p   = isOnVal;
            const char *end = buf + n;
            bool v          = true;

            if (jsonReadBool(p, end, &v)) {
                _isOn = v;
            }
        }

        return true;
    }

    void AppStateStore::writeFile()
    {
        File f = SPIFFS.open(APP_STATE_TMP_PATH, "w");

        if (!f) {
            D_PRINTLN("[APP_STATE] failed to open tmp file");

            return;
        }

        char buf[64];
        int len = snprintf(buf, sizeof(buf),
                           "{\"schemaVersion\":%u,\"isOn\":%s}\n",
                           (unsigned)APP_STATE_SCHEMA,
                           _isOn ? "true" : "false");

        if (len > 0) f.write((const uint8_t *)buf, (size_t)len);

        f.close();

        SPIFFS.remove(APP_STATE_PATH);
        SPIFFS.rename(APP_STATE_TMP_PATH, APP_STATE_PATH);
    }
}  // namespace Lightnet
