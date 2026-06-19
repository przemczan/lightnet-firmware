#include "SceneStore.hpp"
#include "../../../Utils/EntryId.hpp"
#include <string.h>

namespace Lightnet {
    using Session = FsStoreCore<SceneCodec>::Session;

    bool SceneStore::idExistsCb(const char *id, void *ctx)
    {
        auto *self = static_cast<const SceneStore *>(ctx);

        return self->existsImpl(id);
    }

    const char * SceneStore::oneShotId() const
    {
        return Lightnet::oneShotId();
    }

    bool SceneStore::isHiddenId(const char *id) const
    {
        return id && (strcmp(id, oneShotId()) == 0);
    }

    bool SceneStore::exists(const char *id) const
    {
        if (!id || !isValidId(id)) return false;

        Session session(_core);

        if (!session.isReady()) return false;

        return existsImpl(id);
    }

    SceneStoreResult SceneStore::get(const char *id, SceneRecord& out) const
    {
        if (!id) return SCENE_STORE_NULL_ARG;

        if (!isValidId(id)) return SCENE_STORE_INVALID_ID;

        Session session(_core);

        if (!session.isReady()) return SCENE_STORE_DB;

        return getImpl(id, out);
    }

    SceneStoreResult SceneStore::create(const SceneRecord& record)
    {
        if (isHiddenId(record.id) && !record.hidden) return SCENE_STORE_HIDDEN;

        if (!isValidId(record.id)) return SCENE_STORE_INVALID_ID;

        if (SceneCodec::serialize(record, _core.scratchBuffer, sizeof(_core.scratchBuffer)) !=
            SCENE_CODEC_OK) {
            return SCENE_STORE_CODEC;
        }

        Session session(_core);

        if (!session.isReady()) return SCENE_STORE_DB;

        return createImpl(record);
    }

    SceneStoreResult SceneStore::update(const char *id, const SceneRecord& record)
    {
        if (!id) return SCENE_STORE_NULL_ARG;

        if (isHiddenId(id) && !record.hidden) return SCENE_STORE_HIDDEN;

        if (!isValidId(id) || !isValidId(record.id)) return SCENE_STORE_INVALID_ID;

        if (strcmp(id, record.id) != 0) return SCENE_STORE_ID_CHANGED;

        if (SceneCodec::serialize(record, _core.scratchBuffer, sizeof(_core.scratchBuffer)) !=
            SCENE_CODEC_OK) {
            return SCENE_STORE_CODEC;
        }

        Session session(_core);

        if (!session.isReady()) return SCENE_STORE_DB;

        return updateImpl(id, record);
    }

    SceneStoreResult SceneStore::remove(const char *id)
    {
        if (!id || isHiddenId(id)) return SCENE_STORE_HIDDEN;

        if (!isValidId(id)) return SCENE_STORE_INVALID_ID;

        Session session(_core);

        if (!session.isReady()) return SCENE_STORE_DB;

        return removeImpl(id);
    }

    SceneStoreResult SceneStore::foreachRecord(RecordCallback callback, void *userContext) const
    {
        Session session(_core);

        if (!session.isReady()) return SCENE_STORE_DB;

        return foreachRecordImpl(callback, userContext);
    }

    uint16_t SceneStore::count() const
    {
        Session session(_core);

        if (!session.isReady()) return 0;

        return countImpl();
    }

    bool SceneStore::allocateId(char *out, size_t outLen) const
    {
        Session session(_core);

        if (!session.isReady()) return false;

        return generateRandomId(out, outLen, idExistsCb, (void *)this);
    }

    bool SceneStore::existsImpl(const char *id) const
    {
        RecordRef recordRef;

        return findById(id, recordRef) == SCENE_STORE_OK;
    }

    SceneStoreResult SceneStore::getImpl(const char *id, SceneRecord& out) const
    {
        RecordRef recordRef;

        SceneStoreResult lookupResult = findById(id, recordRef);

        if (lookupResult != SCENE_STORE_OK) return lookupResult;

        DatabaseResult readResult = _core.database.read(recordRef, out, _core.scratchBuffer);

        return mapDatabaseResult(readResult);
    }

    SceneStoreResult SceneStore::createImpl(const SceneRecord& record)
    {
        RecordRef recordRef;

        if (findById(record.id, recordRef) == SCENE_STORE_OK) {
            return SCENE_STORE_ID_EXISTS;
        }

        DatabaseResult insertResult = _core.database.insert(
            record, _core.scratchBuffer, &recordRef);

        return mapDatabaseResult(insertResult);
    }

    SceneStoreResult SceneStore::updateImpl(const char *id, const SceneRecord& record)
    {
        RecordRef recordRef;

        SceneStoreResult lookupResult = findById(id, recordRef);

        if (lookupResult != SCENE_STORE_OK) return lookupResult;

        DatabaseResult replaceResult = _core.database.replace(
            recordRef, record, _core.scratchBuffer);

        return mapDatabaseResult(replaceResult);
    }

    SceneStoreResult SceneStore::removeImpl(const char *id)
    {
        RecordRef recordRef;

        SceneStoreResult lookupResult = findById(id, recordRef);

        if (lookupResult != SCENE_STORE_OK) return lookupResult;

        DatabaseResult removeResult = _core.database.remove(recordRef);

        return mapDatabaseResult(removeResult);
    }

    SceneStoreResult SceneStore::foreachRecordImpl(RecordCallback callback, void *userContext) const
    {
        if (!callback) return SCENE_STORE_NULL_ARG;

        struct IterationState {
            RecordCallback   callback;
            void *           userContext;
            SceneRecord      record;
            SceneStoreResult storeResult;
        } state = { callback, userContext, {}, SCENE_STORE_OK };

        DatabaseResult foreachResult = _core.database.foreachLive(
            [&](RecordRef recordRef, const uint8_t *payload) {
            (void)recordRef;

            if (SceneCodec::deserialize(payload, SceneCodec::RECORD_SIZE, state.record) !=
                SCENE_CODEC_OK) {
                state.storeResult = SCENE_STORE_CODEC;

                return DB_FOREACH_ABORTED;
            }

            if (state.record.hidden) return DB_OK;

            state.callback(state.record, state.userContext);

            return DB_OK;
        });

        if (state.storeResult != SCENE_STORE_OK) return state.storeResult;

        return mapDatabaseResult(foreachResult);
    }

    uint16_t SceneStore::countImpl() const
    {
        return _core.database.liveCount();
    }

    SceneStoreResult SceneStore::findById(const char *id, RecordRef& recordRef) const
    {
        struct LookupState {
            const char *searchId;
            RecordRef * recordRef;
            bool        found;
        } lookup = { id, &recordRef, false };

        DatabaseResult foreachResult = _core.database.foreachLive(
            [&](RecordRef candidateRef, const uint8_t *payload) {
            if (!SceneCodec::recordIdMatches(payload, SceneCodec::RECORD_SIZE, lookup.searchId)) {
                return DB_OK;
            }

            *lookup.recordRef = candidateRef;
            lookup.found      = true;

            return DB_OK;
        });

        if (foreachResult != DB_OK) return mapDatabaseResult(foreachResult);

        return lookup.found ? SCENE_STORE_OK : SCENE_STORE_NOT_FOUND;
    }

    SceneStoreResult SceneStore::mapDatabaseResult(DatabaseResult databaseResult)
    {
        if (databaseResult == DB_OK) return SCENE_STORE_OK;

        if (databaseResult == DB_STORAGE_OPEN_FAILED) return SCENE_STORE_FS_OPEN;

        return SCENE_STORE_DB;
    }
}  // namespace Lightnet
