#pragma once

#include "SceneCodec.hpp"
#include "../../../Common/Database/FsStoreCore.hpp"

namespace Lightnet {
    enum SceneStoreResult : uint8_t {
        SCENE_STORE_OK           = 0,
        SCENE_STORE_INVALID_ID   = 1,
        SCENE_STORE_NOT_FOUND    = 2,
        SCENE_STORE_FS_OPEN      = 3,
        SCENE_STORE_DB           = 4,
        SCENE_STORE_CODEC        = 5,
        SCENE_STORE_NULL_ARG     = 6,
        SCENE_STORE_ID_EXISTS    = 7,
        SCENE_STORE_ID_CHANGED   = 8,
        SCENE_STORE_HIDDEN       = 9,
    };

    class SceneStore
    {
        public:
            static constexpr size_t MAX_SCENE_BYTES = 4096;

            typedef void (*RecordCallback)(const SceneRecord& record, void *userContext);

            bool               exists(const char *id) const;
            SceneStoreResult   get(const char *id, SceneRecord& out) const;
            SceneStoreResult   create(const SceneRecord& record);
            SceneStoreResult   update(const char *id, const SceneRecord& record);
            SceneStoreResult   remove(const char *id);
            SceneStoreResult   foreachRecord(RecordCallback callback, void *userContext) const;
            uint16_t           count() const;
            bool               allocateId(char *out, size_t outLen) const;

            const char * oneShotId() const;
            bool         isHiddenId(const char *id) const;

        private:
            static constexpr const char *SCENE_DATABASE_PATH = "/data/scenes.db";
            static constexpr const char *SCENE_DATA_DIR      = "/data";

            mutable FsStoreCore<SceneCodec> _core{
                SCENE_DATABASE_PATH, SCENE_DATA_DIR
            };

            bool             existsImpl(const char *id) const;
            SceneStoreResult getImpl(const char *id, SceneRecord& out) const;
            SceneStoreResult createImpl(const SceneRecord& record);
            SceneStoreResult updateImpl(const char *id, const SceneRecord& record);
            SceneStoreResult removeImpl(const char *id);
            SceneStoreResult foreachRecordImpl(RecordCallback callback, void *userContext) const;
            uint16_t         countImpl() const;

            SceneStoreResult findById(const char *id, RecordRef& recordRef) const;
            static SceneStoreResult mapDatabaseResult(DatabaseResult databaseResult);
            static bool             idExistsCb(const char *id, void *ctx);
    };
}  // namespace Lightnet
