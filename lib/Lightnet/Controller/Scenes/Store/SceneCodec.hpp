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

        static uint8_t serialize(const SceneRecord& record, uint8_t *buffer, size_t capacity);
        static uint8_t deserialize(const uint8_t *buffer, size_t length, SceneRecord& out);

        // Id is the first field in the serialized record — compare without deserializing
        // the full ~3 KB model (avoids nested SceneRecord frames on small task stacks).
        static bool recordIdMatches(const uint8_t *buffer, size_t length, const char *id);
    };
}  // namespace Lightnet
