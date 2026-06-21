#include "AppearanceCodec.hpp"
#include <string.h>

namespace Lightnet {
    uint8_t AppearanceCodec::serialize(const AppearanceRecord& record, uint8_t *buffer, size_t capacity)
    {
        if (capacity < RECORD_SIZE) return APPEARANCE_CODEC_BUF_TOO_SMALL;

        memset(buffer, 0, capacity);
        memcpy(buffer, &record, RECORD_SIZE);

        return APPEARANCE_CODEC_OK;
    }

    uint8_t AppearanceCodec::deserialize(const uint8_t *buffer, size_t length, AppearanceRecord& out)
    {
        if (length < RECORD_SIZE) return APPEARANCE_CODEC_BUF_TOO_SMALL;

        memcpy(&out, buffer, RECORD_SIZE);
        out.palette[sizeof(out.palette) - 1] = '\0';

        return APPEARANCE_CODEC_OK;
    }
}  // namespace Lightnet
