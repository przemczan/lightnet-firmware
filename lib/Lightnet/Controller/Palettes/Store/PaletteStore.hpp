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

            // Resumable single-record read for chunked HTTP responses. Pass
            // Lightnet::RECORDS_START_OFFSET as fromSlotOffset to begin. On return with
            // foundOut=true, recordOut is populated and nextSlotOffsetOut should be
            // passed on the next call. Opens and closes its own short-lived Session
            // per call, so the palette store lock is held only for one record's
            // lookup+read — never hold a cursor across calls without going through here.
            PaletteStoreResult nextRecord(
                size_t         fromSlotOffset,
                PaletteRecord& recordOut,
                size_t&        nextSlotOffsetOut,
                bool&          foundOut
            ) const;

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
