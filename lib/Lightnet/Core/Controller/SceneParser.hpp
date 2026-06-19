#pragma once

#include <stddef.h>
#include <stdint.h>
#include "SceneRecord.hpp"

namespace Lightnet {
    // Parse `len` bytes of scene JSON from `json` into `out`.
    // On failure returns false and writes a message into `errMsg` (if provided).
    bool parseScene(const char *json, size_t len, SceneRecord& out, char *errMsg, size_t errLen);

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
