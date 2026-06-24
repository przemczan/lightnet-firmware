#pragma once
// Insert or replace a top-level string field in a JSON object without full re-serialize.

#include <stddef.h>
#include <stdint.h>
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
            int quotedLen      = jsonQuotedLen(value);

            if (prefixLen + quotedLen + suffixLen + 1 > (int)outCap) return -1;

            memcpy(out, body, (size_t)prefixLen);

            if (jsonWriteQuotedString(out + prefixLen, outCap - (size_t)prefixLen, value) < 0) {
                return -1;
            }

            memcpy(out + prefixLen + (size_t)quotedLen, valEnd, (size_t)suffixLen);
            out[prefixLen + quotedLen + suffixLen] = '\0';

            return prefixLen + quotedLen + suffixLen;
        }

        const char *insertAt = body;

        jsonSkipWs(insertAt, end);

        if (insertAt >= end || *insertAt != '{') return -1;

        insertAt++;

        jsonSkipWs(insertAt, end);

        size_t fieldLen = strlen(field);
        size_t quotedValueLen = jsonQuotedLen(value);
        size_t insertLen = 1 + fieldLen + 1 + 1 + quotedValueLen + 1;
        size_t prefixLen = (size_t)(insertAt - body);
        size_t suffixLen = (size_t)(end - insertAt);

        if (prefixLen + insertLen + suffixLen + 1 > outCap) return -1;

        memcpy(out, body, prefixLen);

        size_t pos = prefixLen;

        out[pos++] = '"';
        memcpy(out + pos, field, fieldLen);
        pos += fieldLen;
        out[pos++] = '"';
        out[pos++] = ':';

        if (jsonWriteQuotedString(out + pos, outCap - pos, value) < 0) return -1;

        pos += quotedValueLen;
        out[pos++] = ',';
        memcpy(out + pos, insertAt, suffixLen);
        pos += suffixLen;
        out[pos] = '\0';

        return (int)pos;
    }
}  // namespace Lightnet
