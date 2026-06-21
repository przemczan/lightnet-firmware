#include "AppearanceStore.hpp"
#include "../../Utils/Debug.hpp"
#include "../../Core/Common/UserColors.hpp"
#include <Arduino.h>
#include <string.h>

namespace Lightnet {
    AppearanceStore::AppearanceStore()
        : writer(10000)
    {
        writeDefaults();
    }

    void AppearanceStore::writeDefaults()
    {
        _record.brightness    = 255;
        _record.baseColors[0] = { 0xFF, 0xFF, 0xFF }; // white primary
        _record.baseColors[1] = { 0x00, 0x00, 0x00 }; // black secondary
        _record.baseColors[2] = { 0x00, 0x00, 0x00 }; // black tertiary
        strncpy(_record.palette, USER_COLORS_PALETTE_NAME, sizeof(_record.palette));
        _record.palette[sizeof(_record.palette) - 1] = '\0';
    }

    Protocol::ColorRGB AppearanceStore::baseColor(uint8_t slot) const
    {
        if (slot >= BASE_COLORS_COUNT) return Protocol::ColorRGB{ 0, 0, 0 };

        return _record.baseColors[slot];
    }

    void AppearanceStore::load()
    {
        if (!_store.load(_record)) {
            D_PRINTLN("[APPEARANCE] no valid record; writing defaults");
            writeDefaults();
            writeFile();
        }
    }

    void AppearanceStore::writeFile()
    {
        if (!_store.save(_record)) {
            D_PRINTLN("[APPEARANCE] failed to persist record");
        }
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

    void AppearanceStore::setBrightness(uint8_t value)
    {
        _record.brightness = value;
        writer.markDirty(millis());
    }

    bool AppearanceStore::setBaseColor(uint8_t slot, Protocol::ColorRGB color)
    {
        if (slot >= BASE_COLORS_COUNT) return false;

        _record.baseColors[slot] = color;
        writer.markDirty(millis());

        return true;
    }

    void AppearanceStore::setAllBaseColors(const Protocol::ColorRGB colors[BASE_COLORS_COUNT])
    {
        for (uint8_t i = 0; i < BASE_COLORS_COUNT; i++) {
            _record.baseColors[i] = colors[i];
        }

        writer.markDirty(millis());
    }

    void AppearanceStore::setPalette(const char *name)
    {
        if (!name) return;

        strncpy(_record.palette, name, sizeof(_record.palette));
        _record.palette[sizeof(_record.palette) - 1] = '\0';
        writer.markDirty(millis());
    }
}  // namespace Lightnet
