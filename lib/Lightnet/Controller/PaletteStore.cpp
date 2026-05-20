#include "PaletteStore.hpp"
#include <string.h>

namespace Lightnet {

namespace {

struct BuiltInPalette {
    const char*        name;
    const GradientStop stops[PALETTE_STOPS];
    uint8_t            count;
};

// Built-in gradient palettes. Position is 0..255 across the full range.
// Each palette must have its first stop at 0 and last stop at 255 for the
// interpolator to cover the entire space.
const BuiltInPalette BUILTINS[] = {
    {
        "rainbow",
        { {0, 0xFF, 0, 0}, {42, 0xFF, 0xFF, 0}, {85, 0, 0xFF, 0},
          {128, 0, 0xFF, 0xFF}, {170, 0, 0, 0xFF}, {213, 0xFF, 0, 0xFF},
          {255, 0xFF, 0, 0} },
        7
    },
    {
        "lava",
        { {0, 0, 0, 0}, {46, 0x24, 0, 0}, {96, 0x71, 0x11, 0},
          {148, 0x8E, 0x03, 0x01}, {204, 0xFF, 0x47, 0x02}, {255, 0xFF, 0xFF, 0xFF} },
        6
    },
    {
        "ocean",
        { {0, 0, 0, 0x10}, {64, 0, 0x20, 0x60}, {128, 0, 0x60, 0xA0},
          {192, 0x10, 0xC0, 0xE0}, {255, 0xFF, 0xFF, 0xFF} },
        5
    },
    {
        "forest",
        { {0, 0, 0x10, 0}, {64, 0, 0x40, 0x10}, {128, 0x10, 0x80, 0x20},
          {192, 0x60, 0xC0, 0x40}, {255, 0xC0, 0xFF, 0x80} },
        5
    },
    {
        "party",
        { {0, 0x55, 0, 0xFF}, {64, 0xFF, 0, 0x80}, {128, 0xFF, 0x80, 0},
          {192, 0xFF, 0xFF, 0}, {255, 0, 0xFF, 0xFF} },
        5
    },
    {
        "sunset",
        { {0, 0x10, 0, 0x40}, {64, 0x80, 0x10, 0x40}, {128, 0xFF, 0x40, 0x10},
          {192, 0xFF, 0xA0, 0x10}, {255, 0xFF, 0xE0, 0x40} },
        5
    },
    {
        "aurora",
        { {0, 0, 0x10, 0x20}, {64, 0, 0x80, 0x60}, {128, 0x20, 0xFF, 0xA0},
          {192, 0x80, 0x40, 0xC0}, {255, 0xFF, 0x40, 0xFF} },
        5
    },
    {
        "embers",
        { {0, 0, 0, 0}, {64, 0x20, 0, 0}, {128, 0x80, 0x10, 0},
          {192, 0xFF, 0x40, 0}, {255, 0xFF, 0xC0, 0x40} },
        5
    },
};

const uint8_t BUILTINS_COUNT = sizeof(BUILTINS) / sizeof(BUILTINS[0]);

}  // anonymous namespace

PaletteStore::PaletteStore() {}

bool PaletteStore::resolve(const char* name, GradientStop* outStops, uint8_t& outCount) const
{
    if (!name) return false;
    for (uint8_t i = 0; i < BUILTINS_COUNT; i++) {
        if (strcmp(name, BUILTINS[i].name) == 0) {
            outCount = BUILTINS[i].count;
            for (uint8_t j = 0; j < outCount; j++) {
                outStops[j] = BUILTINS[i].stops[j];
            }
            return true;
        }
    }
    return false;
}

bool PaletteStore::exists(const char* name) const
{
    if (!name) return false;
    if (strcmp(name, "userColors") == 0) return true;  // synthesized
    for (uint8_t i = 0; i < BUILTINS_COUNT; i++) {
        if (strcmp(name, BUILTINS[i].name) == 0) return true;
    }
    return false;
}

void PaletteStore::buildUserColors(const Protocol::ColorRGB baseColors[BASE_COLORS_COUNT],
                                    GradientStop* outStops, uint8_t& outCount)
{
    outStops[0] = { 0,   baseColors[0].r, baseColors[0].g, baseColors[0].b };
    outStops[1] = { 128, baseColors[1].r, baseColors[1].g, baseColors[1].b };
    outStops[2] = { 255, baseColors[2].r, baseColors[2].g, baseColors[2].b };
    outCount = 3;
}

uint8_t PaletteStore::builtInCount() const { return BUILTINS_COUNT; }

const char* PaletteStore::builtInName(uint8_t i) const
{
    if (i >= BUILTINS_COUNT) return nullptr;
    return BUILTINS[i].name;
}

}  // namespace Lightnet
