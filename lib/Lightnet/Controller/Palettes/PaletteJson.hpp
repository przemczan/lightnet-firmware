#pragma once
// Pure JSON parser for palette bodies — no SPIFFS, no Arduino dependency.
// Split out from PaletteStore so it can be unit-tested under `pio test -e native`.

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "../../Common/Palette.hpp"
#include "../../Common/LightnetConfig.hpp"
#include "../../Utils/SimpleJson.hpp"

namespace Lightnet {
// Parse a palette JSON body of the form:
//   {"schemaVersion":1,"name":"foo","stops":[[pos,"#RRGGBB"],...]}
//
// On success returns true and fills outStops/outCount with up to PALETTE_STOPS
// entries. If outName/outNameLen are provided, the parsed name is copied there;
// in that case the parse fails when the JSON has no "name" field.
//
// Whitespace inside the JSON is tolerated (cursor helpers skip it).
    inline bool parsePaletteJson(
        const char *  json,
        size_t        len,
        GradientStop *outStops,
        uint8_t&      outCount,
        char *        outName = nullptr,
        size_t        outNameLen = 0
    )
    {
        outCount = 0;
        if (outName && outNameLen > 0) outName[0] = '\0';

        if (!json || len == 0) return false;

        const char *p = json;
        const char *end = json + len;

        if (!jsonEnterObject(p, end)) return false;

        char key[32];
        while (jsonNextKey(p, end, key, sizeof(key))) {
            if (strcmp(key, "name") == 0) {
                if (outName) {
                    if (!jsonReadString(p, end, outName, outNameLen)) return false;
                } else {
                    jsonSkipValue(p, end);
                }
            } else if (strcmp(key, "stops") == 0) {
                if (!jsonEnterArray(p, end)) return false;

                while (outCount < PALETTE_STOPS && jsonNextElement(p, end)) {
                    if (!jsonEnterArray(p, end)) return false;

                    long pos;

                    if (!jsonNextElement(p, end) || !jsonReadUInt(p, end, &pos) || pos > 255) return false;

                    char hex[16];
                    uint8_t r, g, b;

                    if (!jsonNextElement(p, end) ||
                        !jsonReadString(p, end, hex, sizeof(hex)) ||
                        !jsonParseHexColor(hex, strlen(hex), &r, &g, &b)) return false;

                    // Inner array must end after exactly two elements.
                    if (jsonNextElement(p, end)) return false;

                    outStops[outCount++] = { (uint8_t)pos, r, g, b };
                }
            } else {
                jsonSkipValue(p, end);
            }
        }

        if (outCount < 1) return false;
        if (outName && outNameLen > 0 && outName[0] == '\0') return false;

        return true;
    }
}  // namespace Lightnet
