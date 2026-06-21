#pragma once

#include "AppStateRecord.hpp"
#include <stddef.h>
#include <stdint.h>

namespace Lightnet {
    enum AppStateCodecResult : uint8_t {
        APP_STATE_CODEC_OK            = 0,
        APP_STATE_CODEC_BUF_TOO_SMALL = 1,
    };

    // The on-disk payload is the raw AppStateRecord (serialize/deserialize are memcpy);
    // deserialize null-terminates the id field so a corrupt record stays bounded.
    struct AppStateCodec {
        typedef AppStateRecord Model;

        static constexpr uint8_t MODEL_VERSION = 1;
        static constexpr size_t  RECORD_SIZE   = sizeof(AppStateRecord);
        static constexpr size_t  SCRATCH_SIZE  = RECORD_SIZE;

        static uint8_t serialize(const AppStateRecord& record, uint8_t *buffer, size_t capacity);
        static uint8_t deserialize(const uint8_t *buffer, size_t length, AppStateRecord& out);
    };
}  // namespace Lightnet
