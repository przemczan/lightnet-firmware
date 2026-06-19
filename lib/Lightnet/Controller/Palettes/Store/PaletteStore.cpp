#include "PaletteStore.hpp"
#include <string.h>

namespace Lightnet {
    using Session = FsStoreCore<PaletteCodec>::Session;

    PaletteStoreResult PaletteStore::load()
    {
        StoreLock::Guard guard(_core.lock);

        _core.reset();

        DatabaseResult openResult = _core.ensureOpen(PALETTE_DATABASE_PATH, PALETTE_DATA_DIR);

        _core.reset();

        return mapDatabaseResult(openResult);
    }

    PaletteStoreResult PaletteStore::seedMissing(const PaletteRecord *records, uint16_t count)
    {
        if (!records || count == 0) return PALETTE_STORE_NULL_ARG;

        Session session(_core, PALETTE_DATABASE_PATH, PALETTE_DATA_DIR);

        if (!session.isReady()) return PALETTE_STORE_DB;

        for (uint16_t i = 0; i < count; i++) {
            RecordRef recordRef;

            if (findByName(records[i].name, recordRef) == PALETTE_STORE_OK) continue;

            if (PaletteCodec::serialize(
                    records[i], session.scratchBuffer(), PaletteCodec::RECORD_SIZE) !=
                PALETTE_CODEC_OK) {
                return PALETTE_STORE_CODEC;
            }

            DatabaseResult insertResult = session.database().insert(
                records[i], session.scratchBuffer(), &recordRef);

            if (insertResult != DB_OK) return mapDatabaseResult(insertResult);
        }

        return PALETTE_STORE_OK;
    }

    bool PaletteStore::exists(const char *name) const
    {
        if (!isValidPaletteName(name)) return false;

        Session session(_core, PALETTE_DATABASE_PATH, PALETTE_DATA_DIR);

        if (!session.isReady()) return false;

        RecordRef recordRef;

        return findByName(name, recordRef) == PALETTE_STORE_OK;
    }

    PaletteStoreResult PaletteStore::get(const char *name, PaletteRecord& out) const
    {
        if (!name) return PALETTE_STORE_NULL_ARG;

        if (!isValidPaletteName(name)) return PALETTE_STORE_INVALID_NAME;

        Session session(_core, PALETTE_DATABASE_PATH, PALETTE_DATA_DIR);

        if (!session.isReady()) return PALETTE_STORE_DB;

        RecordRef recordRef;

        PaletteStoreResult lookupResult = findByName(name, recordRef);

        if (lookupResult != PALETTE_STORE_OK) return lookupResult;

        DatabaseResult readResult = session.database().read(
            recordRef, out, session.scratchBuffer());

        return mapDatabaseResult(readResult);
    }

    PaletteStoreResult PaletteStore::create(const PaletteRecord& record)
    {
        if (!isValidPaletteName(record.name)) return PALETTE_STORE_INVALID_NAME;

        if (PaletteCodec::serialize(record, _core.scratchBuffer, sizeof(_core.scratchBuffer)) !=
            PALETTE_CODEC_OK) {
            return PALETTE_STORE_CODEC;
        }

        Session session(_core, PALETTE_DATABASE_PATH, PALETTE_DATA_DIR);

        if (!session.isReady()) return PALETTE_STORE_DB;

        RecordRef recordRef;

        if (findByName(record.name, recordRef) == PALETTE_STORE_OK) {
            return PALETTE_STORE_NAME_EXISTS;
        }

        DatabaseResult insertResult = session.database().insert(
            record, session.scratchBuffer(), &recordRef);

        return mapDatabaseResult(insertResult);
    }

    PaletteStoreResult PaletteStore::update(const char *name, const PaletteRecord& record)
    {
        if (!name) return PALETTE_STORE_NULL_ARG;

        if (!isValidPaletteName(name) || !isValidPaletteName(record.name)) {
            return PALETTE_STORE_INVALID_NAME;
        }

        if (strcmp(name, record.name) != 0) return PALETTE_STORE_NAME_CHANGED;

        if (PaletteCodec::serialize(record, _core.scratchBuffer, sizeof(_core.scratchBuffer)) !=
            PALETTE_CODEC_OK) {
            return PALETTE_STORE_CODEC;
        }

        Session session(_core, PALETTE_DATABASE_PATH, PALETTE_DATA_DIR);

        if (!session.isReady()) return PALETTE_STORE_DB;

        RecordRef recordRef;

        PaletteStoreResult lookupResult = findByName(name, recordRef);

        if (lookupResult != PALETTE_STORE_OK) return lookupResult;

        DatabaseResult replaceResult = session.database().replace(
            recordRef, record, session.scratchBuffer());

        return mapDatabaseResult(replaceResult);
    }

    PaletteStoreResult PaletteStore::remove(const char *name)
    {
        if (!name) return PALETTE_STORE_NULL_ARG;

        if (!isValidPaletteName(name)) return PALETTE_STORE_INVALID_NAME;

        Session session(_core, PALETTE_DATABASE_PATH, PALETTE_DATA_DIR);

        if (!session.isReady()) return PALETTE_STORE_DB;

        RecordRef recordRef;

        PaletteStoreResult lookupResult = findByName(name, recordRef);

        if (lookupResult != PALETTE_STORE_OK) return lookupResult;

        DatabaseResult removeResult = session.database().remove(recordRef);

        return mapDatabaseResult(removeResult);
    }

    PaletteStoreResult PaletteStore::foreachRecord(RecordCallback callback, void *userContext) const
    {
        if (!callback) return PALETTE_STORE_NULL_ARG;

        Session session(_core, PALETTE_DATABASE_PATH, PALETTE_DATA_DIR);

        if (!session.isReady()) return PALETTE_STORE_DB;

        struct IterationState {
            RecordCallback     callback;
            void *             userContext;
            PaletteRecord      record;
            PaletteStoreResult storeResult;
        } state = { callback, userContext, {}, PALETTE_STORE_OK };

        DatabaseResult foreachResult = session.database().foreachLive(
            [&](RecordRef recordRef, const uint8_t *payload) {
            (void)recordRef;

            if (PaletteCodec::deserialize(payload, PaletteCodec::RECORD_SIZE, state.record) !=
                PALETTE_CODEC_OK) {
                state.storeResult = PALETTE_STORE_CODEC;

                return DB_FOREACH_ABORTED;
            }

            state.callback(state.record, state.userContext);

            return DB_OK;
        });

        if (state.storeResult != PALETTE_STORE_OK) return state.storeResult;

        return mapDatabaseResult(foreachResult);
    }

    uint16_t PaletteStore::count() const
    {
        Session session(_core, PALETTE_DATABASE_PATH, PALETTE_DATA_DIR);

        if (!session.isReady()) return 0;

        return session.database().liveCount();
    }

    PaletteStoreResult PaletteStore::findByName(const char *name, RecordRef& recordRef) const
    {
        struct LookupState {
            const char *searchName;
            RecordRef * recordRef;
            bool        found;
        } lookup = { name, &recordRef, false };

        DatabaseResult foreachResult = _core.database.foreachLive(
            [&](RecordRef candidateRef, const uint8_t *payload) {
            PaletteRecord candidateRecord;

            if (PaletteCodec::deserialize(payload, PaletteCodec::RECORD_SIZE, candidateRecord) !=
                PALETTE_CODEC_OK) {
                return DB_OK;
            }

            if (!paletteNamesEqual(candidateRecord.name, lookup.searchName)) {
                return DB_OK;
            }

            *lookup.recordRef = candidateRef;
            lookup.found      = true;

            return DB_OK;
        });

        if (foreachResult != DB_OK) return mapDatabaseResult(foreachResult);

        return lookup.found ? PALETTE_STORE_OK : PALETTE_STORE_NOT_FOUND;
    }

    PaletteStoreResult PaletteStore::mapDatabaseResult(DatabaseResult databaseResult)
    {
        if (databaseResult == DB_OK) return PALETTE_STORE_OK;

        if (databaseResult == DB_STORAGE_OPEN_FAILED) return PALETTE_STORE_FS_OPEN;

        return PALETTE_STORE_DB;
    }
}  // namespace Lightnet
