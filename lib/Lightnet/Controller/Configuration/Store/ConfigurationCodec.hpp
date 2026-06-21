#pragma once

#include "ConfigurationRecord.hpp"
#include <stddef.h>
#include <stdint.h>

namespace Lightnet {
    enum ConfigurationCodecResult : uint8_t {
        CONFIGURATION_CODEC_OK            = 0,
        CONFIGURATION_CODEC_BUF_TOO_SMALL = 1,
        CONFIGURATION_CODEC_INVALID       = 2,
    };

    // The on-disk payload is the raw ConfigurationRecord (serialize/deserialize are memcpy),
    // so the store reads/writes the record directly through the session scratch buffer.
    struct ConfigurationCodec {
        typedef ConfigurationRecord Model;

        static constexpr uint8_t MODEL_VERSION = 1;
        static constexpr size_t  RECORD_SIZE   = sizeof(ConfigurationRecord);
        static constexpr size_t  SCRATCH_SIZE  = RECORD_SIZE;

        static uint8_t serialize(const ConfigurationRecord& record, uint8_t *buffer, size_t capacity);
        static uint8_t deserialize(const uint8_t *buffer, size_t length, ConfigurationRecord& out);
    };
}  // namespace Lightnet
