#pragma once

#include "Store/PaletteStore.hpp"
#include "../../Core/Controller/IPaletteResolver.hpp"
#include "../../Core/Common/Palette.hpp"
#include "../../Core/Common/UserColors.hpp"

namespace Lightnet {
    class PaletteRepository : public IPaletteResolver
    {
        public:
            void ensureSeeded();

            // Returns true for stored palettes, built-ins, and USER_COLORS_PALETTE_NAME.
            bool exists(const char *name) const;

            bool isBuiltin(const char *name) const;

            // IPaletteResolver — returns false for USER_COLORS_PALETTE_NAME (callers
            // synthesize that via buildUserColors()) and for unknown names.
            bool resolve(const char *name, GradientStop *outStops, uint8_t& outCount) const override;

            PaletteStoreResult get(const char *name, PaletteRecord& out) const;
            PaletteStoreResult create(const PaletteRecord& record);
            PaletteStoreResult update(const char *name, const PaletteRecord& record);
            PaletteStoreResult remove(const char *name);

            // Iterates USER_COLORS_PALETTE_NAME, all hardcoded built-ins, then stored records.
            // The "Base colors" entry has stopsCount == 0 (no stops stored).
            typedef void (*RecordCallback)(const PaletteRecord& record, void *userContext);
            void foreachRecord(RecordCallback callback, void *userContext) const;

            uint16_t count() const;

        private:
            PaletteStore _store;
    };
}  // namespace Lightnet
