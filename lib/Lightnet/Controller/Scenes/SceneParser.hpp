#pragma once

#include <stdint.h>
#include "../../Common/Protocol.hpp"
#include "../../Common/LightnetConfig.hpp"
#include "ScenePlayer.hpp"

namespace Lightnet {
// Result of parsing a scene JSON body. Stack-allocated in route handlers (~2.2 KB).
    struct SceneParseResult {
        bool               valid;
        char               errMsg[64];

        // Scene fields
        uint8_t            schemaVersion;
        char               name[20];
        bool               loop;
        char               palette[16]; // scene-level default palette name
        Protocol::ColorRGB baseColors[BASE_COLORS_COUNT];

        uint8_t            layerCount;
        SceneLayer         layers[SCENE_MAX_LAYERS];
    };

// Parse `len` bytes of scene JSON from `json` into `out`.
// Returns true on success; on failure out.valid is false and out.errMsg is set.
    bool parseScene(const char *json, size_t len, SceneParseResult& out);

// Parse a flat one-shot body {"group":N,"panels":...,...step fields...} into a
// single-layer SceneLayer. Returns false + errMsg on validation failure.
    bool parseOneShotBody(
        const char *json,
        size_t      len,
        SceneLayer& layer,
        char *      errMsg,
        size_t      errLen
    );
}  // namespace Lightnet
