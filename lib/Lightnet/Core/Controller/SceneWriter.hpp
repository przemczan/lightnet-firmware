#pragma once

#include <stddef.h>
#include "SceneRecord.hpp"

namespace Lightnet {
    // Serialize `record` to scene JSON. Returns bytes written (excluding NUL), or -1 on error.
    int serializeScene(const SceneRecord& record, char *buf, size_t bufLen);
}  // namespace Lightnet
