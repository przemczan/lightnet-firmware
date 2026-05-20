#include "AppearanceStore.hpp"
#include "../Utils/Debug.hpp"
#include <Arduino.h>

#include <FS.h>
#ifdef ARDUINO_ARCH_ESP32
    #include <SPIFFS.h>
#endif

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

namespace Lightnet {

namespace {

const char* APPEARANCE_PATH      = "/config/appearance.json";
const char* APPEARANCE_TMP_PATH  = "/config/appearance.json.tmp";
const uint8_t APPEARANCE_SCHEMA  = 1;

// Skip whitespace from `*p`.
void skipWs(const char*& p, const char* end) {
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
}

// If `*p` points at `key` (a quoted JSON key like "brightness"), advance past it and the colon.
// Returns true if matched, false otherwise (leaves *p unchanged).
bool matchKey(const char*& p, const char* end, const char* key) {
    const char* start = p;
    skipWs(p, end);
    size_t klen = strlen(key);
    if (p + klen + 2 > end || *p != '"') { p = start; return false; }
    if (strncmp(p + 1, key, klen) != 0 || p[1 + klen] != '"') { p = start; return false; }
    p += klen + 2;
    skipWs(p, end);
    if (p >= end || *p != ':') { p = start; return false; }
    p++;
    skipWs(p, end);
    return true;
}

// Parse "#RRGGBB" hex string into RGB. Returns true on success.
bool parseHexColor(const char* s, size_t len, Protocol::ColorRGB* out) {
    if (len != 7 || s[0] != '#') return false;
    auto h = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    int r1 = h(s[1]), r0 = h(s[2]);
    int g1 = h(s[3]), g0 = h(s[4]);
    int b1 = h(s[5]), b0 = h(s[6]);
    if (r1 < 0 || r0 < 0 || g1 < 0 || g0 < 0 || b1 < 0 || b0 < 0) return false;
    out->r = (uint8_t)((r1 << 4) | r0);
    out->g = (uint8_t)((g1 << 4) | g0);
    out->b = (uint8_t)((b1 << 4) | b0);
    return true;
}

void formatHexColor(Protocol::ColorRGB c, char* out) {
    snprintf(out, 8, "#%02X%02X%02X", c.r, c.g, c.b);
}

}  // anonymous namespace

AppearanceStore::AppearanceStore(AnimationScheduler& _scheduler, const PaletteStore& _palettes)
    : scheduler(_scheduler), palettes(_palettes)
{
    writeDefaults();
}

void AppearanceStore::writeDefaults()
{
    brightnessValue = 255;
    baseColorsValue[0] = { 0xFF, 0xFF, 0xFF };  // white primary
    baseColorsValue[1] = { 0x00, 0x00, 0x00 };  // black secondary
    baseColorsValue[2] = { 0x00, 0x00, 0x00 };  // black tertiary
    strncpy(paletteValue, "userColors", sizeof(paletteValue));
    paletteValue[sizeof(paletteValue) - 1] = '\0';
}

Protocol::ColorRGB AppearanceStore::baseColor(uint8_t slot) const
{
    if (slot >= BASE_COLORS_COUNT) return Protocol::ColorRGB{0, 0, 0};
    return baseColorsValue[slot];
}

void AppearanceStore::loadAndApply()
{
    bool valid = readFile();
    if (!valid) {
        PRINTLN("[APPEARANCE] no valid file; writing defaults");
        writeDefaults();
        writeFile();
    }

    // Broadcast to panels: brightness first (cheap), then base colors, then palette.
    scheduler.broadcastGlobalBrightness(brightnessValue);
    scheduler.broadcastBaseColors(baseColorsValue);
    broadcastSelectedPalette();
}

bool AppearanceStore::readFile()
{
    if (!SPIFFS.exists(APPEARANCE_PATH)) return false;

    File f = SPIFFS.open(APPEARANCE_PATH, "r");
    if (!f) return false;

    // Whole file fits comfortably in a small stack buffer (~150 B for max contents).
    char buf[512];
    size_t n = f.readBytes(buf, sizeof(buf) - 1);
    f.close();
    buf[n] = '\0';

    const char* p = buf;
    const char* end = buf + n;

    // Defaults stay if any field is missing.
    writeDefaults();

    // Find opening brace and walk through known fields. This isn't a full JSON
    // parser — we look for the known keys in any order, with very lenient
    // formatting. Anything unrecognized is skipped to the next key.
    bool sawSchema = false;
    uint8_t schema = 1;

    while (p < end) {
        skipWs(p, end);
        if (p >= end) break;

        if (matchKey(p, end, "schemaVersion")) {
            schema = (uint8_t)strtol(p, (char**)&p, 10);
            sawSchema = true;
        } else if (matchKey(p, end, "brightness")) {
            long v = strtol(p, (char**)&p, 10);
            if (v >= 0 && v <= 255) brightnessValue = (uint8_t)v;
        } else if (matchKey(p, end, "palette")) {
            if (p < end && *p == '"') {
                p++;
                const char* nameStart = p;
                while (p < end && *p != '"') p++;
                size_t len = (size_t)(p - nameStart);
                if (len > 0 && len < sizeof(paletteValue)) {
                    memcpy(paletteValue, nameStart, len);
                    paletteValue[len] = '\0';
                }
                if (p < end) p++;
            }
        } else if (matchKey(p, end, "baseColors")) {
            // Expect a JSON array of up to 3 hex color strings.
            skipWs(p, end);
            if (p < end && *p == '[') {
                p++;
                for (uint8_t i = 0; i < BASE_COLORS_COUNT && p < end; i++) {
                    skipWs(p, end);
                    if (*p == ']') break;
                    if (*p == '"') {
                        p++;
                        const char* s = p;
                        while (p < end && *p != '"') p++;
                        size_t len = (size_t)(p - s);
                        Protocol::ColorRGB c;
                        if (parseHexColor(s, len, &c)) {
                            baseColorsValue[i] = c;
                        }
                        if (p < end) p++;  // closing quote
                    }
                    skipWs(p, end);
                    if (p < end && *p == ',') p++;
                }
                while (p < end && *p != ']') p++;
                if (p < end) p++;  // closing bracket
            }
        } else {
            // Unknown char — advance past it so we don't loop forever.
            p++;
        }
    }

    if (sawSchema && schema != APPEARANCE_SCHEMA) {
        PRINTLN("[APPEARANCE] schema mismatch — falling back to defaults");
        writeDefaults();
        writeFile();  // overwrite with current schema
    }

    return true;
}

void AppearanceStore::writeFile()
{
    // Ensure /config/ exists. SPIFFS is flat namespace so directories don't have to be
    // created, but FS implementations that emulate them benefit from the explicit dir.
    // (On ESP8266 SPIFFS this is a no-op.)
    File f = SPIFFS.open(APPEARANCE_TMP_PATH, "w");
    if (!f) {
        PRINTLN("[APPEARANCE] failed to open tmp file");
        return;
    }

    char hex0[8], hex1[8], hex2[8];
    formatHexColor(baseColorsValue[0], hex0);
    formatHexColor(baseColorsValue[1], hex1);
    formatHexColor(baseColorsValue[2], hex2);

    char buf[256];
    int len = snprintf(buf, sizeof(buf),
        "{\n"
        "  \"schemaVersion\": %u,\n"
        "  \"brightness\": %u,\n"
        "  \"baseColors\": [\"%s\", \"%s\", \"%s\"],\n"
        "  \"palette\": \"%s\"\n"
        "}\n",
        (unsigned)APPEARANCE_SCHEMA,
        (unsigned)brightnessValue,
        hex0, hex1, hex2,
        paletteValue);

    if (len > 0) {
        f.write((const uint8_t*)buf, (size_t)len);
    }
    f.close();

    SPIFFS.remove(APPEARANCE_PATH);
    SPIFFS.rename(APPEARANCE_TMP_PATH, APPEARANCE_PATH);
}

void AppearanceStore::broadcastSelectedPalette()
{
    GradientStop stops[PALETTE_STOPS];
    uint8_t count = 0;

    if (strcmp(paletteValue, "userColors") == 0) {
        PaletteStore::buildUserColors(baseColorsValue, stops, count);
    } else if (!palettes.resolve(paletteValue, stops, count)) {
        // Unknown palette — fall back to userColors so we still produce valid output.
        PaletteStore::buildUserColors(baseColorsValue, stops, count);
    }

    scheduler.broadcastPalette(stops, count);
}

bool AppearanceStore::setBrightness(uint8_t value)
{
    brightnessValue = value;
    writeFile();
    scheduler.broadcastGlobalBrightness(value);
    return true;
}

bool AppearanceStore::setBaseColor(uint8_t slot, Protocol::ColorRGB color)
{
    if (slot >= BASE_COLORS_COUNT) return false;
    baseColorsValue[slot] = color;
    writeFile();
    scheduler.broadcastBaseColors(baseColorsValue);
    // If the active palette is userColors, the visible color also changes — re-push.
    if (strcmp(paletteValue, "userColors") == 0) {
        broadcastSelectedPalette();
    }
    return true;
}

bool AppearanceStore::setAllBaseColors(const Protocol::ColorRGB colors[BASE_COLORS_COUNT])
{
    for (uint8_t i = 0; i < BASE_COLORS_COUNT; i++) {
        baseColorsValue[i] = colors[i];
    }
    writeFile();
    scheduler.broadcastBaseColors(baseColorsValue);
    if (strcmp(paletteValue, "userColors") == 0) {
        broadcastSelectedPalette();
    }
    return true;
}

bool AppearanceStore::setPalette(const char* name)
{
    if (!name) return false;
    if (!palettes.exists(name)) return false;
    strncpy(paletteValue, name, sizeof(paletteValue));
    paletteValue[sizeof(paletteValue) - 1] = '\0';
    writeFile();
    broadcastSelectedPalette();
    return true;
}

}  // namespace Lightnet
