#include "PaletteRepository.hpp"
#include <string.h>

namespace Lightnet {
    namespace {
        struct BuiltinPalette {
            const char *       name;
            const GradientStop stops[PALETTE_STOPS];
            uint8_t            count;
        };

        const BuiltinPalette BUILTINS[] = {
            {
                "Rainbow",
                { { 0, 0xFF, 0, 0 }, { 42, 0xFF, 0xFF, 0 }, { 85, 0, 0xFF, 0 },
                    { 128, 0, 0xFF, 0xFF }, { 170, 0, 0, 0xFF }, { 213, 0xFF, 0, 0xFF },
                    { 255, 0xFF, 0, 0 } },
                7
            },
            {
                "Lava",
                { { 0, 0, 0, 0 }, { 46, 0x24, 0, 0 }, { 96, 0x71, 0x11, 0 },
                    { 148, 0x8E, 0x03, 0x01 }, { 204, 0xFF, 0x47, 0x02 }, { 255, 0xFF, 0xFF, 0xFF } },
                6
            },
            {
                "Ocean",
                { { 0, 0, 0, 0x10 }, { 64, 0, 0x20, 0x60 }, { 128, 0, 0x60, 0xA0 },
                    { 192, 0x10, 0xC0, 0xE0 }, { 255, 0xFF, 0xFF, 0xFF } },
                5
            },
            {
                "Forest",
                { { 0, 0, 0x10, 0 }, { 64, 0, 0x40, 0x10 }, { 128, 0x10, 0x80, 0x20 },
                    { 192, 0x60, 0xC0, 0x40 }, { 255, 0xC0, 0xFF, 0x80 } },
                5
            },
            {
                "Party",
                { { 0, 0x55, 0, 0xFF }, { 64, 0xFF, 0, 0x80 }, { 128, 0xFF, 0x80, 0 },
                    { 192, 0xFF, 0xFF, 0 }, { 255, 0, 0xFF, 0xFF } },
                5
            },
            {
                "Sunset",
                { { 0, 0x10, 0, 0x40 }, { 64, 0x80, 0x10, 0x40 }, { 128, 0xFF, 0x40, 0x10 },
                    { 192, 0xFF, 0xA0, 0x10 }, { 255, 0xFF, 0xE0, 0x40 } },
                5
            },
            {
                "Aurora",
                { { 0, 0, 0x10, 0x20 }, { 64, 0, 0x80, 0x60 }, { 128, 0x20, 0xFF, 0xA0 },
                    { 192, 0x80, 0x40, 0xC0 }, { 255, 0xFF, 0x40, 0xFF } },
                5
            },
            {
                "Embers",
                { { 0, 0, 0, 0 }, { 64, 0x20, 0, 0 }, { 128, 0x80, 0x10, 0 },
                    { 192, 0xFF, 0x40, 0 }, { 255, 0xFF, 0xC0, 0x40 } },
                5
            },
        };

        const uint8_t BUILTINS_COUNT = sizeof(BUILTINS) / sizeof(BUILTINS[0]);
    } // anonymous namespace

    void PaletteRepository::ensureSeeded()
    {
        PaletteRecord records[BUILTINS_COUNT];

        for (uint8_t i = 0; i < BUILTINS_COUNT; i++) {
            records[i] = {};

            strncpy(records[i].name, BUILTINS[i].name, sizeof(records[i].name) - 1);
            records[i].builtin    = true;
            records[i].stopsCount = BUILTINS[i].count;

            for (uint8_t j = 0; j < BUILTINS[i].count; j++) {
                records[i].stops[j] = BUILTINS[i].stops[j];
            }
        }

        _store.seedMissing(records, BUILTINS_COUNT);
    }

    bool PaletteRepository::exists(const char *name) const
    {
        if (!name || !name[0]) return false;

        if (paletteNamesEqual(name, USER_COLORS_PALETTE_NAME)) return true;

        return _store.exists(name);
    }

    bool PaletteRepository::isBuiltin(const char *name) const
    {
        if (!name || !name[0]) return false;

        if (paletteNamesEqual(name, USER_COLORS_PALETTE_NAME)) return true;

        PaletteRecord record;

        if (_store.get(name, record) == PALETTE_STORE_OK) return record.builtin;

        return false;
    }

    bool PaletteRepository::resolve(const char *name, GradientStop *outStops, uint8_t& outCount) const
    {
        if (!name || !outStops) return false;

        if (paletteNamesEqual(name, USER_COLORS_PALETTE_NAME)) return false;

        PaletteRecord record;

        if (_store.get(name, record) == PALETTE_STORE_OK) {
            outCount = record.stopsCount;

            for (uint8_t i = 0; i < outCount; i++) {
                outStops[i] = record.stops[i];
            }

            return true;
        }

        return false;
    }

    PaletteStoreResult PaletteRepository::get(const char *name, PaletteRecord& out) const
    {
        return _store.get(name, out);
    }

    PaletteStoreResult PaletteRepository::create(const PaletteRecord& record)
    {
        return _store.create(record);
    }

    PaletteStoreResult PaletteRepository::update(const char *name, const PaletteRecord& record)
    {
        if (!name) return PALETTE_STORE_NULL_ARG;

        if (isBuiltin(name)) return PALETTE_STORE_NOT_FOUND;

        return _store.update(name, record);
    }

    PaletteStoreResult PaletteRepository::remove(const char *name)
    {
        if (!name) return PALETTE_STORE_NULL_ARG;

        if (isBuiltin(name)) return PALETTE_STORE_NOT_FOUND;

        return _store.remove(name);
    }

    void PaletteRepository::foreachRecord(RecordCallback callback, void *userContext) const
    {
        if (!callback) return;

        PaletteRecord userColors = {};

        strncpy(userColors.name, USER_COLORS_PALETTE_NAME, sizeof(userColors.name) - 1);
        userColors.builtin    = true;
        userColors.stopsCount = 0;

        callback(userColors, userContext);

        _store.foreachRecord(callback, userContext);
    }

    uint16_t PaletteRepository::count() const
    {
        return 1 + _store.count();
    }

    PaletteStoreResult PaletteRepository::nextRecord(
        size_t         fromSlotOffset,
        PaletteRecord& recordOut,
        size_t&        nextSlotOffsetOut,
        bool&          foundOut
    ) const
    {
        return _store.nextRecord(fromSlotOffset, recordOut, nextSlotOffsetOut, foundOut);
    }
}  // namespace Lightnet
