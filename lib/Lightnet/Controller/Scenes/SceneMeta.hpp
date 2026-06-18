#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../../Utils/EntryId.hpp"
#include "../../Utils/SimpleJson.hpp"

namespace Lightnet {
    struct SceneMeta {
        char     id[ENTRY_ID_MAX + 1];
        char     name[65];
        uint8_t  layersNum;
        uint32_t duration;
    };

    inline bool parseSceneMeta(const char *json, size_t len, SceneMeta& out)
    {
        memset(&out, 0, sizeof(out));

        if (!json || len == 0) return false;

        SimpleJson j(json, len);

        long schema = j.getInt("schemaVersion");

        if (schema != 1) return false;

        if (!j.getString("id", out.id, sizeof(out.id)) || !isValidId(out.id)) return false;

        if (!j.getString("name", out.name, sizeof(out.name)) || out.name[0] == '\0') return false;

        long layers = j.getInt("layersNum");

        if (layers < 0 || layers > 255) return false;

        out.layersNum = (uint8_t)layers;

        long duration = j.getInt("duration");

        if (duration < 0) return false;

        out.duration = (uint32_t)duration;

        return true;
    }

    inline int serializeSceneMeta(const SceneMeta& meta, char *buf, size_t bufLen)
    {
        return snprintf(buf, bufLen,
                        "{\"schemaVersion\":1,\"id\":\"%s\",\"name\":\"%s\",\"layersNum\":%u,\"duration\":%lu}",
                        meta.id, meta.name, (unsigned)meta.layersNum, (unsigned long)meta.duration);
    }
}  // namespace Lightnet
