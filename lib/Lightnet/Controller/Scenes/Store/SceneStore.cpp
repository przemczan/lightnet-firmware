#include "SceneStore.hpp"
#include "../../../Utils/EntryId.hpp"
#include "../../../Common/Database/DatabaseFormat.hpp"
#include "../../Store/StorageSliceReader.hpp"
#include <stddef.h>
#include <stdlib.h>
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

        // On-disk form is the raw record; no serialize step. Validate the record directly
        // (what serialize() used to do internally) before writing it.
        if (!SceneCodec::isValid(record)) return SCENE_STORE_CODEC;

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

        // On-disk form is the raw record; no serialize step. Validate directly.
        if (!SceneCodec::isValid(record)) return SCENE_STORE_CODEC;

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

    SceneStoreResult SceneStore::foreachMeta(MetaCallback callback, void *userContext) const
    {
        Session session(_core);

        if (!session.isReady()) return SCENE_STORE_DB;

        return foreachMetaImpl(callback, userContext);
    }

    uint16_t SceneStore::count() const
    {
        Session session(_core);

        if (!session.isReady()) return 0;

        return countImpl();
    }

    SceneStoreResult SceneStore::compactIfFragmented()
    {
        Session session(_core);

        if (!session.isReady()) return SCENE_STORE_DB;

        Database<SceneCodec>& db    = session.database();
        const uint16_t live  = db.liveCount();
        const size_t slots = db.slotCount();

        if (slots <= (size_t)live + 1) return SCENE_STORE_OK;

        uint8_t *scratch = (uint8_t *)malloc(SceneCodec::RECORD_SIZE);

        if (!scratch) return SCENE_STORE_DB;

        DatabaseResult compactResult = db.compact(scratch);

        free(scratch);

        return mapDatabaseResult(compactResult);
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

        StorageSliceReader slice(_core.storage, recordPayloadOffset(recordRef.offset),
                                 SceneCodec::RECORD_SIZE);

        if (slice.read((uint8_t *)&out, SceneCodec::RECORD_SIZE) != (int)SceneCodec::RECORD_SIZE) {
            return SCENE_STORE_DB;
        }

        out.id[ENTRY_ID_MAX]                       = '\0';
        out.name[sizeof(out.name) - 1]             = '\0';
        out.palette[sizeof(out.palette) - 1]       = '\0';

        if (!SceneCodec::isValid(out)) return SCENE_STORE_CODEC;

        return SCENE_STORE_OK;
    }

    SceneStoreResult SceneStore::getForPlay(
        const char * id,
        SceneHeader& headerOut,
        SceneLayer * layersOut,
        uint8_t      maxLayers
    ) const
    {
        if (!id || !layersOut) return SCENE_STORE_NULL_ARG;

        Session session(_core);

        if (!session.isReady()) return SCENE_STORE_DB;

        RecordRef recordRef;

        SceneStoreResult lookupResult = findById(id, recordRef);

        if (lookupResult != SCENE_STORE_OK) return lookupResult;

        const size_t base = recordPayloadOffset(recordRef.offset);

        // Small header read (~84 B): everything except the layers array.
        StorageSliceReader headerReader(_core.storage, base, sizeof(SceneHeader));

        if (headerReader.read((uint8_t *)&headerOut, sizeof(SceneHeader)) != (int)sizeof(SceneHeader)) {
            return SCENE_STORE_DB;
        }

        headerOut.id[ENTRY_ID_MAX]                       = '\0';
        headerOut.name[sizeof(headerOut.name) - 1]       = '\0';
        headerOut.palette[sizeof(headerOut.palette) - 1] = '\0';

        uint8_t count = headerOut.layerCount;

        if (count > maxLayers) count = maxLayers;

        headerOut.layerCount = count;

        // Stream the layers straight into the caller's buffer (e.g. ScenePlayer's own layers[]).
        // `layers` immediately follows the packed header, so it starts at sizeof(SceneHeader).
        if (count > 0) {
            const size_t layersOffset = base + sizeof(SceneHeader);
            const size_t bytes        = (size_t)count * sizeof(SceneLayer);

            StorageSliceReader layersReader(_core.storage, layersOffset, bytes);

            if (layersReader.read((uint8_t *)layersOut, bytes) != (int)bytes) {
                return SCENE_STORE_DB;
            }
        }

        return SCENE_STORE_OK;
    }

    SceneStoreResult SceneStore::createImpl(const SceneRecord& record)
    {
        RecordRef recordRef;

        if (findById(record.id, recordRef) == SCENE_STORE_OK) {
            return SCENE_STORE_ID_EXISTS;
        }

        DatabaseResult insertResult = _core.database.insertSerialized(
            (const uint8_t *)&record, &recordRef);

        return mapDatabaseResult(insertResult);
    }

    SceneStoreResult SceneStore::updateImpl(const char *id, const SceneRecord& record)
    {
        RecordRef recordRef;

        SceneStoreResult lookupResult = findById(id, recordRef);

        if (lookupResult != SCENE_STORE_OK) return lookupResult;

        DatabaseResult replaceResult = _core.database.replaceSerialized(
            recordRef, (const uint8_t *)&record);

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

    SceneStoreResult SceneStore::foreachMetaImpl(MetaCallback callback, void *userContext) const
    {
        if (!callback) return SCENE_STORE_NULL_ARG;

        struct IterationState {
            MetaCallback callback;
            void *       userContext;
        } state = { callback, userContext };

        // Read SceneMeta prefix + the hidden byte that immediately follows it.
        constexpr size_t readSize = offsetof(SceneRecord, hidden) + sizeof(uint8_t);

        DatabaseResult foreachResult = _core.database.foreachLive(
            [&](RecordRef recordRef, IRandomAccessStorage& storage, size_t payloadOffset, size_t /*payloadSize*/) {
            (void)recordRef;

            uint8_t buf[readSize];

            if (storage.read(buf, readSize) != readSize) return DB_OK;

            if (buf[offsetof(SceneRecord, hidden)]) return DB_OK;

            SceneMeta meta;
            memcpy(&meta, buf, sizeof(SceneMeta));

            state.callback(meta, state.userContext);

            return DB_OK;
        });

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
            [&](RecordRef candidateRef, IRandomAccessStorage& storage, size_t payloadOffset, size_t /*payloadSize*/) {
            char storedId[sizeof(SceneMeta::id)];
            StorageSliceReader slice(storage, payloadOffset, sizeof(SceneMeta::id));

            if (slice.read((uint8_t *)storedId, sizeof(SceneMeta::id)) != sizeof(SceneMeta::id))return DB_OK;

            storedId[ENTRY_ID_MAX] = '\0';

            if (strcmp(storedId, lookup.searchId) != 0) return DB_OK;

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
