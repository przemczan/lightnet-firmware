#include "AppStateCodec.hpp"
#include <string.h>

namespace Lightnet {
    uint8_t AppStateCodec::serialize(const AppStateRecord& record, uint8_t *buffer, size_t capacity)
    {
        if (capacity < RECORD_SIZE) return APP_STATE_CODEC_BUF_TOO_SMALL;

        memset(buffer, 0, capacity);
        memcpy(buffer, &record, RECORD_SIZE);

        return APP_STATE_CODEC_OK;
    }

    uint8_t AppStateCodec::deserialize(const uint8_t *buffer, size_t length, AppStateRecord& out)
    {
        if (length < RECORD_SIZE) return APP_STATE_CODEC_BUF_TOO_SMALL;

        memcpy(&out, buffer, RECORD_SIZE);
        out.lastPlayedSceneId[ENTRY_ID_MAX] = '\0';

        return APP_STATE_CODEC_OK;
    }
}  // namespace Lightnet
