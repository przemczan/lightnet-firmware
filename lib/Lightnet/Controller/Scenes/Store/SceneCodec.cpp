#include "SceneCodec.hpp"
#include "../../../Utils/EntryId.hpp"
#include "../../../Core/Controller/ScenePlayer.hpp"
#include <string.h>

namespace Lightnet {
    namespace {
        bool isValidRecord(const SceneRecord& record)
        {
            if (!record.hidden && !isValidId(record.id)) return false;

            if (record.hidden && strcmp(record.id, oneShotId()) != 0) return false;

            if (!isValidSceneName(record.name)) return false;

            if (record.layerCount == 0 || record.layerCount > SCENE_MAX_LAYERS) return false;

            if (record.schemaVersion > SCENE_SCHEMA_VERSION) return false;

            return true;
        }
    } // anonymous namespace

    uint8_t SceneCodec::serialize(const SceneRecord& record, uint8_t *buffer, size_t capacity)
    {
        if (capacity < RECORD_SIZE) return SCENE_CODEC_BUF_TOO_SMALL;

        if (!isValidRecord(record)) return SCENE_CODEC_INVALID;

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

        if (!isValidRecord(out)) {
            if (!out.hidden && !isValidId(out.id)) return SCENE_CODEC_INVALID_ID;

            return SCENE_CODEC_INVALID;
        }

        return SCENE_CODEC_OK;
    }

    bool SceneCodec::recordIdMatches(const uint8_t *buffer, size_t length, const char *id)
    {
        if (!buffer || !id || length < ENTRY_ID_MAX + 1) return false;

        char storedId[ENTRY_ID_MAX + 1];

        memcpy(storedId, buffer, ENTRY_ID_MAX + 1);
        storedId[ENTRY_ID_MAX] = '\0';

        return strcmp(storedId, id) == 0;
    }
}  // namespace Lightnet
