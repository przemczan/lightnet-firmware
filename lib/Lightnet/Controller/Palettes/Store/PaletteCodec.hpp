#pragma once

#include "PaletteRecord.hpp"
#include <stddef.h>
#include <stdint.h>

namespace Lightnet {
    enum PaletteCodecResult : uint8_t {
        PALETTE_CODEC_OK              = 0,
        PALETTE_CODEC_BUF_TOO_SMALL   = 1,
        PALETTE_CODEC_INVALID_NAME    = 2,
        PALETTE_CODEC_INVALID_STOPS   = 3,
    };

    struct PaletteCodec {
        typedef PaletteRecord Model;

        static constexpr uint8_t MODEL_VERSION = 2;
        static constexpr size_t  RECORD_SIZE     = sizeof(PaletteRecord);

        // PaletteCodec transforms wire<->record (serialize/deserialize are not memcpy), so the
        // store needs a staging buffer. Small (RECORD_SIZE ~ a few hundred bytes).
        static constexpr size_t  SCRATCH_SIZE    = RECORD_SIZE;

        static uint8_t serialize(const PaletteRecord& record, uint8_t *buffer, size_t capacity);
        static uint8_t deserialize(const uint8_t *buffer, size_t length, PaletteRecord& out);
    };
}  // namespace Lightnet
