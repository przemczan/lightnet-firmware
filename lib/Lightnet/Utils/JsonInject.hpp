#pragma once
// Insert or replace a top-level string field in a JSON object without full re-serialize.

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "SimpleJson.hpp"

namespace Lightnet {
    inline bool jsonReadTopLevelString(
        const char *json,
        size_t      len,
        const char *key,
        char *      out,
        size_t      outLen
    )
    {
        if (!json || !key || !out || outLen == 0) return false;

        out[0] = '\0';

        const char *val = jsonFindKey(json, len, key);

        if (!val) return false;

        return jsonParseString(val, json + len, out, outLen);
    }

    inline int jsonUpsertStringField(
        const char *body,
        size_t      bodyLen,
        const char *field,
        const char *value,
        char *      out,
        size_t      outCap
    )
    {
        if (!body || !field || !value || !out || outCap == 0) return -1;

        const char *end    = body + bodyLen;
        const char *valPtr = jsonFindKey(body, bodyLen, field);

        if (valPtr) {
            const char *p        = valPtr;
            const char *valStart = p;

            if (!jsonSkipValue(p, end)) return -1;

            const char *valEnd = p;
            int prefixLen      = (int)(valStart - body);
            int suffixLen      = (int)(end - valEnd);
            char quoted[96];

            int quotedLen = snprintf(quoted, sizeof(quoted), "\"%s\"", value);

            if (quotedLen <= 0 || quotedLen >= (int)sizeof(quoted)) return -1;

            if (prefixLen + quotedLen + suffixLen + 1 > (int)outCap) return -1;

            memcpy(out, body, (size_t)prefixLen);
            memcpy(out + prefixLen, quoted, (size_t)quotedLen);
            memcpy(out + prefixLen + (size_t)quotedLen, valEnd, (size_t)suffixLen);
            out[prefixLen + quotedLen + suffixLen] = '\0';

            return prefixLen + quotedLen + suffixLen;
        }

        const char *insertAt = body;

        jsonSkipWs(insertAt, end);

        if (insertAt >= end || *insertAt != '{') return -1;

        insertAt++;

        jsonSkipWs(insertAt, end);

        char insert[96];
        int insertLen = snprintf(insert, sizeof(insert), "\"%s\":\"%s\",", field, value);

        if (insertLen <= 0 || insertLen >= (int)sizeof(insert)) return -1;

        size_t prefixLen = (size_t)(insertAt - body);
        size_t suffixLen = (size_t)(end - insertAt);

        if (prefixLen + (size_t)insertLen + suffixLen + 1 > outCap) return -1;

        memcpy(out, body, prefixLen);
        memcpy(out + prefixLen, insert, (size_t)insertLen);
        memcpy(out + prefixLen + (size_t)insertLen, insertAt, suffixLen);
        out[prefixLen + insertLen + suffixLen] = '\0';

        return (int)(prefixLen + (size_t)insertLen + suffixLen);
    }
}  // namespace Lightnet
