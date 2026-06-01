#include "PaletteStore.hpp"
#include "PaletteJson.hpp"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <FS.h>
#ifdef ARDUINO_ARCH_ESP32
    #include <SPIFFS.h>
#endif

namespace Lightnet {
    namespace {
        struct BuiltInPalette {
            const char *       name;
            const GradientStop stops[PALETTE_STOPS];
            uint8_t            count;
        };

        // Built-in gradient palettes. Position is 0..255 across the full range.
        // Each palette must have its first stop at 0 and last stop at 255 for the
        // interpolator to cover the entire space.
        const BuiltInPalette BUILTINS[] = {
            {
                "rainbow",
                { { 0, 0xFF, 0, 0 }, { 42, 0xFF, 0xFF, 0 }, { 85, 0, 0xFF, 0 },
                    { 128, 0, 0xFF, 0xFF }, { 170, 0, 0, 0xFF }, { 213, 0xFF, 0, 0xFF },
                    { 255, 0xFF, 0, 0 } },
                7
            },
            {
                "lava",
                { { 0, 0, 0, 0 }, { 46, 0x24, 0, 0 }, { 96, 0x71, 0x11, 0 },
                    { 148, 0x8E, 0x03, 0x01 }, { 204, 0xFF, 0x47, 0x02 }, { 255, 0xFF, 0xFF, 0xFF } },
                6
            },
            {
                "ocean",
                { { 0, 0, 0, 0x10 }, { 64, 0, 0x20, 0x60 }, { 128, 0, 0x60, 0xA0 },
                    { 192, 0x10, 0xC0, 0xE0 }, { 255, 0xFF, 0xFF, 0xFF } },
                5
            },
            {
                "forest",
                { { 0, 0, 0x10, 0 }, { 64, 0, 0x40, 0x10 }, { 128, 0x10, 0x80, 0x20 },
                    { 192, 0x60, 0xC0, 0x40 }, { 255, 0xC0, 0xFF, 0x80 } },
                5
            },
            {
                "party",
                { { 0, 0x55, 0, 0xFF }, { 64, 0xFF, 0, 0x80 }, { 128, 0xFF, 0x80, 0 },
                    { 192, 0xFF, 0xFF, 0 }, { 255, 0, 0xFF, 0xFF } },
                5
            },
            {
                "sunset",
                { { 0, 0x10, 0, 0x40 }, { 64, 0x80, 0x10, 0x40 }, { 128, 0xFF, 0x40, 0x10 },
                    { 192, 0xFF, 0xA0, 0x10 }, { 255, 0xFF, 0xE0, 0x40 } },
                5
            },
            {
                "aurora",
                { { 0, 0, 0x10, 0x20 }, { 64, 0, 0x80, 0x60 }, { 128, 0x20, 0xFF, 0xA0 },
                    { 192, 0x80, 0x40, 0xC0 }, { 255, 0xFF, 0x40, 0xFF } },
                5
            },
            {
                "embers",
                { { 0, 0, 0, 0 }, { 64, 0x20, 0, 0 }, { 128, 0x80, 0x10, 0 },
                    { 192, 0xFF, 0x40, 0 }, { 255, 0xFF, 0xC0, 0x40 } },
                5
            },
        };

        const uint8_t BUILTINS_COUNT = sizeof(BUILTINS) / sizeof(BUILTINS[0]);
    } // anonymous namespace

    PaletteStore::PaletteStore()
    {
    }

    bool PaletteStore::resolve(const char *name, GradientStop *outStops, uint8_t& outCount) const
    {
        if (!name) return false;

        // Check built-ins first
        for (uint8_t i = 0; i < BUILTINS_COUNT; i++) {
            if (strcmp(name, BUILTINS[i].name) == 0) {
                outCount = BUILTINS[i].count;

                for (uint8_t j = 0; j < outCount; j++) {
                    outStops[j] = BUILTINS[i].stops[j];
                }

                return true;
            }
        }

        // Fall through to SPIFFS user palettes
        char path[40];

        snprintf(path, sizeof(path), "/palettes/%s.json", name);

        if (!SPIFFS.exists(path)) return false;

        File f = SPIFFS.open(path, "r");

        if (!f) return false;

        char buf[512];
        size_t n = f.readBytes(buf, sizeof(buf) - 1);

        f.close();
        buf[n] = '\0';

        return Lightnet::parsePaletteJson(buf, n, outStops, outCount);
    }

    bool PaletteStore::isBuiltIn(const char *name) const
    {
        if (!name) return false;

        for (uint8_t i = 0; i < BUILTINS_COUNT; i++) {
            if (strcmp(name, BUILTINS[i].name) == 0) return true;
        }

        return false;
    }

    bool PaletteStore::exists(const char *name) const
    {
        if (!name) return false;

        if (strcmp(name, "userColors") == 0) return true;

        if (isBuiltIn(name)) return true;

        char path[40];

        snprintf(path, sizeof(path), "/palettes/%s.json", name);

        return SPIFFS.exists(path);
    }

    void PaletteStore::buildUserColors(
        const Protocol::ColorRGB baseColors[BASE_COLORS_COUNT],
        GradientStop *           outStops,
        uint8_t&                 outCount
    )
    {
        outStops[0] = { 0, baseColors[0].r, baseColors[0].g, baseColors[0].b };
        outStops[1] = { 128, baseColors[1].r, baseColors[1].g, baseColors[1].b };
        outStops[2] = { 255, baseColors[2].r, baseColors[2].g, baseColors[2].b };
        outCount = 3;
    }

    uint8_t PaletteStore::builtInCount() const
    {
        return BUILTINS_COUNT;
    }

    const char * PaletteStore::builtInName(uint8_t i) const
    {
        if (i >= BUILTINS_COUNT) return nullptr;

        return BUILTINS[i].name;
    }

    // Fixed temp path — per-name variants like "/palettes/myname.json.tmp" can
    // exceed SPIFFS's 31-char path limit. See SceneStore.cpp for the same rationale.
    static const char PALETTE_TMP[] = "/palettes/.write.tmp"; // 20 chars

    bool PaletteStore::save(const char *name, const GradientStop *stops, uint8_t count) const
    {
        if (!name || count == 0 || count > PALETTE_STOPS) return false;

        char path[40];

        snprintf(path, sizeof(path), "/palettes/%s.json", name);

        File f = SPIFFS.open(PALETTE_TMP, "w");

        if (!f) return false;

        f.print("{\"schemaVersion\":1,\"name\":\"");
        f.print(name);
        f.print("\",\"stops\":[");

        for (uint8_t i = 0; i < count; i++) {
            if (i) f.print(",");

            char hex[8];

            snprintf(hex, sizeof(hex), "#%02X%02X%02X", stops[i].r, stops[i].g, stops[i].b);
            f.print("[");
            f.print((int)stops[i].pos);
            f.print(",\"");
            f.print(hex);
            f.print("\"]");
        }

        f.print("]}");
        f.close();

        SPIFFS.remove(path);
        SPIFFS.rename(PALETTE_TMP, path);

        return true;
    }

    bool PaletteStore::deleteUserPalette(const char *name) const
    {
        if (!name || isBuiltIn(name)) return false;

        char path[40];

        snprintf(path, sizeof(path), "/palettes/%s.json", name);

        if (!SPIFFS.exists(path)) return false;

        return SPIFFS.remove(path);
    }
}  // namespace Lightnet
