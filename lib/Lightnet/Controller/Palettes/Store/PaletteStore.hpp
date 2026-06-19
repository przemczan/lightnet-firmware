#pragma once

#include "PaletteCodec.hpp"
#include "../../../Common/Database/FsStoreCore.hpp"

namespace Lightnet {
    enum PaletteStoreResult : uint8_t {
        PALETTE_STORE_OK             = 0,
        PALETTE_STORE_INVALID_NAME   = 1,
        PALETTE_STORE_NOT_FOUND      = 2,
        PALETTE_STORE_FS_OPEN        = 3,
        PALETTE_STORE_DB             = 4,
        PALETTE_STORE_CODEC          = 5,
        PALETTE_STORE_NULL_ARG       = 6,
        PALETTE_STORE_NAME_EXISTS    = 7,
        PALETTE_STORE_NAME_CHANGED   = 8,
    };

    class PaletteStore
    {
        public:
            typedef void (*RecordCallback)(const PaletteRecord& record, void *userContext);

            PaletteStoreResult load();
            PaletteStoreResult seedMissing(const PaletteRecord *records, uint16_t count);
            bool               exists(const char *name) const;
            PaletteStoreResult get(const char *name, PaletteRecord& out) const;
            PaletteStoreResult create(const PaletteRecord& record);
            PaletteStoreResult update(const char *name, const PaletteRecord& record);
            PaletteStoreResult remove(const char *name);
            PaletteStoreResult foreachRecord(RecordCallback callback, void *userContext) const;
            uint16_t           count() const;

        private:
            static constexpr const char *PALETTE_DATABASE_PATH = "/data/palettes.db";
            static constexpr const char *PALETTE_DATA_DIR      = "/data";

            mutable FsStoreCore<PaletteCodec> _core{
                PALETTE_DATABASE_PATH, PALETTE_DATA_DIR
            };

            PaletteStoreResult findByName(const char *name, RecordRef& recordRef) const;
            static PaletteStoreResult mapDatabaseResult(DatabaseResult databaseResult);
    };
}  // namespace Lightnet
