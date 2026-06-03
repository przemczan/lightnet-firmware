#include "AppearanceStore.hpp"
#include "../../Utils/Debug.hpp"
#include "../../Utils/SimpleJson.hpp"
#include <Arduino.h>
#include "../../Utils/Fs/Fs.hpp"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

namespace Lightnet {
    namespace {
        const char *APPEARANCE_PATH     = "/config/appearance.json";
        const char *APPEARANCE_TMP_PATH = "/config/appearance.json.tmp";
        const uint8_t APPEARANCE_SCHEMA = 1;
    } // anonymous namespace

    AppearanceStore::AppearanceStore(AnimationScheduler& _scheduler, const PaletteStore& _palettes)
        : scheduler(_scheduler), palettes(_palettes), writer(10000)
    {
        writeDefaults();
    }

    void AppearanceStore::writeDefaults()
    {
        brightnessValue = 255;
        baseColorsValue[0] = { 0xFF, 0xFF, 0xFF }; // white primary
        baseColorsValue[1] = { 0x00, 0x00, 0x00 }; // black secondary
        baseColorsValue[2] = { 0x00, 0x00, 0x00 }; // black tertiary
        strncpy(paletteValue, "userColors", sizeof(paletteValue));
        paletteValue[sizeof(paletteValue) - 1] = '\0';
    }

    Protocol::ColorRGB AppearanceStore::baseColor(uint8_t slot) const
    {
        if (slot >= BASE_COLORS_COUNT) return Protocol::ColorRGB{ 0, 0, 0 };

        return baseColorsValue[slot];
    }

    void AppearanceStore::loadAndApply()
    {
        bool valid = readFile();

        if (!valid) {
            D_PRINTLN("[APPEARANCE] no valid file; writing defaults");
            writeDefaults();
            writeFile();
        }

        // Broadcast to panels: brightness first (cheap), then base colors, then palette.
        scheduler.broadcastGlobalBrightness(brightnessValue);
        scheduler.broadcastBaseColors(baseColorsValue);
        broadcastSelectedPalette();
    }

    void AppearanceStore::reapply()
    {
        scheduler.broadcastGlobalBrightness(brightnessValue);
        scheduler.broadcastBaseColors(baseColorsValue);
        broadcastSelectedPalette();
    }

    bool AppearanceStore::readFile()
    {
        if (!Fs::exists(APPEARANCE_PATH)) return false;

        File f = Fs::open(APPEARANCE_PATH, "r");

        if (!f) return false;

        char buf[512];
        size_t n = f.readBytes(buf, sizeof(buf) - 1);

        f.close();
        buf[n] = '\0';

        writeDefaults(); // ensure any missing field stays at its default

        SimpleJson j(buf, n);

        long schema = j.getInt("schemaVersion");

        if (schema > 0 && schema != APPEARANCE_SCHEMA) {
            D_PRINTLN("[APPEARANCE] schema mismatch — falling back to defaults");
            writeDefaults();
            writeFile();

            return true;
        }

        long brightness = j.getInt("brightness");

        if (brightness >= 0 && brightness <= 255) brightnessValue = (uint8_t)brightness;

        char palName[20];

        if (j.getString("palette", palName, sizeof(palName))) {
            strncpy(paletteValue, palName, sizeof(paletteValue) - 1);
            paletteValue[sizeof(paletteValue) - 1] = '\0';
        }

        // baseColors is an array — parse it from the raw value pointer
        const char *bc = j.rawValue("baseColors");

        if (bc) {
            const char *p   = bc;
            const char *end = buf + n;

            while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;

            if (p < end && *p == '[') {
                p++;

                for (uint8_t i = 0; i < BASE_COLORS_COUNT && p < end; i++) {
                    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ',')) p++;

                    if (p >= end || *p == ']') break;

                    if (*p == '"') {
                        p++;

                        char hex[8] = { 0 };
                        size_t hi = 0;

                        while (p < end && *p != '"' && hi < 7) hex[hi++] = *p++;

                        hex[hi] = '\0';

                        if (p < end) p++;              // skip closing quote

                        uint8_t r, g, b;

                        if (jsonParseHexColor(hex, hi, &r, &g, &b)) {
                            baseColorsValue[i] = { r, g, b };
                        }
                    }
                }
            }
        }

        return true;
    }

    void AppearanceStore::writeFile()
    {
        File f = Fs::open(APPEARANCE_TMP_PATH, "w");

        if (!f) {
            D_PRINTLN("[APPEARANCE] failed to open tmp file");

            return;
        }

        char hex0[8], hex1[8], hex2[8];

        jsonFormatHex(baseColorsValue[0].r, baseColorsValue[0].g, baseColorsValue[0].b, hex0);
        jsonFormatHex(baseColorsValue[1].r, baseColorsValue[1].g, baseColorsValue[1].b, hex1);
        jsonFormatHex(baseColorsValue[2].r, baseColorsValue[2].g, baseColorsValue[2].b, hex2);

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
            f.write((const uint8_t *)buf, (size_t)len);
        }

        f.close();

        Fs::remove(APPEARANCE_PATH);
        Fs::rename(APPEARANCE_TMP_PATH, APPEARANCE_PATH);
    }

    void AppearanceStore::tick(uint32_t now)
    {
        if (writer.shouldFlush(now)) {
            writeFile();
            writer.clear();
        }
    }

    void AppearanceStore::flush()
    {
        if (writer.isDirty()) {
            writeFile();
            writer.clear();
        }
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
        writer.markDirty(millis());
        scheduler.broadcastGlobalBrightness(value);

        return true;
    }

    bool AppearanceStore::setBaseColor(uint8_t slot, Protocol::ColorRGB color)
    {
        if (slot >= BASE_COLORS_COUNT) return false;

        baseColorsValue[slot] = color;
        writer.markDirty(millis());
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

        writer.markDirty(millis());
        scheduler.broadcastBaseColors(baseColorsValue);

        if (strcmp(paletteValue, "userColors") == 0) {
            broadcastSelectedPalette();
        }

        return true;
    }

    bool AppearanceStore::setPalette(const char *name)
    {
        if (!name) return false;

        if (!palettes.exists(name)) return false;

        strncpy(paletteValue, name, sizeof(paletteValue));
        paletteValue[sizeof(paletteValue) - 1] = '\0';
        writer.markDirty(millis());
        broadcastSelectedPalette();

        return true;
    }
}  // namespace Lightnet
