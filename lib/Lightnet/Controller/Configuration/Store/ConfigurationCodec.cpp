#include "ConfigurationCodec.hpp"
#include <string.h>

namespace Lightnet {
    uint8_t ConfigurationCodec::serialize(const ConfigurationRecord& record, uint8_t *buffer, size_t capacity)
    {
        if (capacity < RECORD_SIZE) return CONFIGURATION_CODEC_BUF_TOO_SMALL;

        if (record.powerStateOnBoot > POWER_LAST_STATE) return CONFIGURATION_CODEC_INVALID;

        memset(buffer, 0, capacity);
        memcpy(buffer, &record, RECORD_SIZE);

        return CONFIGURATION_CODEC_OK;
    }

    uint8_t ConfigurationCodec::deserialize(const uint8_t *buffer, size_t length, ConfigurationRecord& out)
    {
        if (length < RECORD_SIZE) return CONFIGURATION_CODEC_BUF_TOO_SMALL;

        memcpy(&out, buffer, RECORD_SIZE);

        if (out.powerStateOnBoot > POWER_LAST_STATE) return CONFIGURATION_CODEC_INVALID;

        return CONFIGURATION_CODEC_OK;
    }
}  // namespace Lightnet
