#pragma once

#include "AppearanceRecord.hpp"
#include <stddef.h>
#include <stdint.h>

namespace Lightnet {
    enum AppearanceCodecResult : uint8_t {
        APPEARANCE_CODEC_OK            = 0,
        APPEARANCE_CODEC_BUF_TOO_SMALL = 1,
    };

    // The on-disk payload is the raw AppearanceRecord (serialize/deserialize are memcpy);
    // deserialize null-terminates the palette name so a corrupt record stays bounded.
    struct AppearanceCodec {
        typedef AppearanceRecord Model;

        static constexpr uint8_t MODEL_VERSION = 1;
        static constexpr size_t  RECORD_SIZE   = sizeof(AppearanceRecord);
        static constexpr size_t  SCRATCH_SIZE  = RECORD_SIZE;

        static uint8_t serialize(const AppearanceRecord& record, uint8_t *buffer, size_t capacity);
        static uint8_t deserialize(const uint8_t *buffer, size_t length, AppearanceRecord& out);
    };
}  // namespace Lightnet
