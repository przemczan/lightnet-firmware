#include "SceneCodec.hpp"
#include "../../../Utils/EntryId.hpp"
#include "../../../Core/Controller/ScenePlayer.hpp"
#include <string.h>

namespace Lightnet {
    bool SceneCodec::isValid(const SceneRecord& record)
    {
        if (!record.hidden && !isValidId(record.id)) return false;

        if (record.hidden && strcmp(record.id, oneShotId()) != 0) return false;

        if (!isValidSceneName(record.name)) return false;

        if (record.layerCount == 0 || record.layerCount > SCENE_MAX_LAYERS) return false;

        if (record.schemaVersion > SCENE_SCHEMA_VERSION) return false;

        return true;
    }

    uint8_t SceneCodec::serialize(const SceneRecord& record, uint8_t *buffer, size_t capacity)
    {
        if (capacity < RECORD_SIZE) return SCENE_CODEC_BUF_TOO_SMALL;

        if (!isValid(record)) return SCENE_CODEC_INVALID;

        memset(buffer, 0, capacity);
        memcpy(buffer, &record, RECORD_SIZE);

        return SCENE_CODEC_OK;
    }

    uint8_t SceneCodec::deserialize(const uint8_t *buffer, size_t length, SceneRecord& out)
    {
        if (length < RECORD_SIZE) return SCENE_CODEC_BUF_TOO_SMALL;

        memcpy(&out, buffer, RECORD_SIZE);
        out.id[ENTRY_ID_MAX]     = '\0';
        out.name[sizeof(out.name) - 1] = '\0';
        out.palette[sizeof(out.palette) - 1] = '\0';

        if (!isValid(out)) {
            if (!out.hidden && !isValidId(out.id)) return SCENE_CODEC_INVALID_ID;

            return SCENE_CODEC_INVALID;
        }

        return SCENE_CODEC_OK;
    }

    bool SceneCodec::recordIdMatches(const uint8_t *buffer, size_t length, const char *id)
    {
        if (!buffer || !id || length < sizeof(SceneMeta::id)) return false;

        char storedId[sizeof(SceneMeta::id)];

        memcpy(storedId, buffer, sizeof(SceneMeta::id));
        storedId[ENTRY_ID_MAX] = '\0';

        return strcmp(storedId, id) == 0;
    }
}  // namespace Lightnet
