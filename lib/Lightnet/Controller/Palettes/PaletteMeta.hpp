#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../../Utils/EntryId.hpp"
#include "../../Utils/SimpleJson.hpp"

namespace Lightnet {
    struct PaletteMeta {
        char id[ENTRY_ID_MAX + 1];
        char name[65];
        bool builtin;
    };

    inline bool parsePaletteMeta(const char *json, size_t len, PaletteMeta& out)
    {
        memset(&out, 0, sizeof(out));

        if (!json || len == 0) return false;

        SimpleJson j(json, len);

        long schema = j.getInt("schemaVersion");

        if (schema != 1) return false;

        if (!j.getString("id", out.id, sizeof(out.id)) || !isValidId(out.id)) return false;

        if (!j.getString("name", out.name, sizeof(out.name)) || out.name[0] == '\0') return false;

        const char *builtinVal = j.rawValue("builtin");

        if (builtinVal) {
            const char *p   = builtinVal;
            const char *end = json + len;

            jsonReadBool(p, end, &out.builtin);
        }

        return true;
    }

    inline int serializePaletteMeta(const PaletteMeta& meta, char *buf, size_t bufLen)
    {
        if (meta.builtin) {
            return snprintf(buf, bufLen,
                            "{\"schemaVersion\":1,\"id\":\"%s\",\"name\":\"%s\",\"builtin\":true}",
                            meta.id, meta.name);
        }

        return snprintf(buf, bufLen,
                        "{\"schemaVersion\":1,\"id\":\"%s\",\"name\":\"%s\"}",
                        meta.id, meta.name);
    }
}  // namespace Lightnet
