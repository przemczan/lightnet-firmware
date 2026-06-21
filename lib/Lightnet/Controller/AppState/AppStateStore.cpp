#include <Arduino.h>
#include "AppStateStore.hpp"
#include "../../Utils/Debug.hpp"
#include <string.h>

namespace Lightnet {
    AppStateStore::AppStateStore()
        : writer(5000)
    {
    }

    void AppStateStore::load()
    {
        if (!_store.load(_record)) {
            D_PRINTLN("[APP_STATE] no valid record; writing defaults");
            writeFile();
        }
    }

    bool AppStateStore::setIsOn(bool value)
    {
        if (value == isOn()) return false;

        _record.isOn = value ? 1 : 0;
        writer.markDirty(millis());

        return true;
    }

    bool AppStateStore::setLastPlayedSceneId(const char *id, bool isStored)
    {
        if (!id) id = "";

        if (strncmp(_record.lastPlayedSceneId, id, sizeof(_record.lastPlayedSceneId) - 1) == 0 &&
            lastPlayedSceneIsStored() == isStored) return false;

        strncpy(_record.lastPlayedSceneId, id, sizeof(_record.lastPlayedSceneId) - 1);
        _record.lastPlayedSceneId[sizeof(_record.lastPlayedSceneId) - 1] = '\0';
        _record.lastPlayedSceneIsStored = isStored ? 1 : 0;
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

    void AppStateStore::writeFile()
    {
        if (!_store.save(_record)) {
            D_PRINTLN("[APP_STATE] failed to persist record");
        }
    }
}  // namespace Lightnet
