#pragma once

#include "../../../Core/Controller/SceneRecord.hpp"
#include <stddef.h>
#include <stdint.h>

namespace Lightnet {
    enum SceneCodecResult : uint8_t {
        SCENE_CODEC_OK            = 0,
        SCENE_CODEC_BUF_TOO_SMALL = 1,
        SCENE_CODEC_INVALID_ID    = 2,
        SCENE_CODEC_INVALID       = 3,
    };

    struct SceneCodec {
        typedef SceneRecord Model;

        static constexpr uint8_t MODEL_VERSION = 1;
        static constexpr size_t  RECORD_SIZE   = sizeof(SceneRecord);

        // The on-disk payload is the raw SceneRecord (serialize/deserialize are memcpy), so
        // SceneStore reads and writes directly to/from the record — no staging buffer needed.
        // FsStoreCore therefore allocates no scratch for scenes (~3 KB saved on ESP8266).
        static constexpr size_t  SCRATCH_SIZE  = 0;

        static uint8_t serialize(const SceneRecord& record, uint8_t *buffer, size_t capacity);
        static uint8_t deserialize(const uint8_t *buffer, size_t length, SceneRecord& out);

        // Validate a SceneRecord that was read directly into memory (e.g. via StorageSliceReader)
        // without going through deserialize. Callers must null-terminate string fields first.
        static bool isValid(const SceneRecord& record);

        // Id is the first field in the serialized record — compare without deserializing
        // the full ~3 KB model (avoids nested SceneRecord frames on small task stacks).
        static bool recordIdMatches(const uint8_t *buffer, size_t length, const char *id);
    };
}  // namespace Lightnet
