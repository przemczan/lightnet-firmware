#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "../Common/ProtocolTypes.hpp"
#include "../Common/LightnetConfig.hpp"
#include "ScenePlayer.hpp"
#include "../../Utils/EntryId.hpp"

namespace Lightnet {
    static const uint8_t SCENE_NAME_MAX = 30;

    inline bool isValidSceneName(const char *name)
    {
        if (!name || name[0] == '\0') return false;

        return strlen(name) <= SCENE_NAME_MAX;
    }

    // Persisted scene model: list index fields + parsed scene content (~2.5–3 KB).
    struct SceneRecord {
        char               id[ENTRY_ID_MAX + 1];
        char               name[SCENE_NAME_MAX + 1];
        uint32_t           duration;
        uint8_t            hidden;

        uint8_t            schemaVersion;
        bool               loop;
        char               palette[16];
        bool               hasPalette;
        bool               hasColors;
        Protocol::ColorRGB baseColors[BASE_COLORS_COUNT];
        Protocol::ColorRGB background;
        float              speed;
        uint8_t            layerCount;
        SceneLayer         layers[SCENE_MAX_LAYERS];
    } __attribute__((packed));
}  // namespace Lightnet
