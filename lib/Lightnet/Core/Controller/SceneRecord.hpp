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

    // Index fields readable without loading the full ~3 KB record.
    // This struct is the exact binary prefix of SceneRecord — reading sizeof(SceneMeta)
    // bytes from disk yields all fields needed for listing and lookup.
    struct SceneMeta {
        char     id[ENTRY_ID_MAX + 1];
        char     name[SCENE_NAME_MAX + 1];
        uint32_t duration;
        uint8_t  schemaVersion;
        uint8_t  layerCount;
    } __attribute__((packed));

    static_assert(sizeof(SceneMeta) == 48, "SceneMeta size mismatch");

    // Everything in SceneRecord except the (~2.2 KB) layers array. This small prefix is what
    // the playback path reads on its own (via StorageSliceReader) so it can stream the layers
    // straight into ScenePlayer's own buffer — no ~3 KB intermediate SceneRecord. SceneMeta is
    // its leading prefix; SceneRecord appends the layers, keeping one packed on-disk layout.
    struct SceneHeader : SceneMeta {
        uint8_t            hidden;
        bool               loop;
        char               palette[16];
        bool               hasPalette;
        bool               hasColors;
        Protocol::ColorRGB baseColors[BASE_COLORS_COUNT];
        Protocol::ColorRGB background;
        float              speed;
    } __attribute__((packed));

    // Full persisted scene model (~3 KB). SceneHeader is the leading prefix; `layers` follows
    // immediately (both packed), so the layers start exactly at sizeof(SceneHeader) on disk.
    struct SceneRecord : SceneHeader {
        SceneLayer         layers[SCENE_MAX_LAYERS];
    } __attribute__((packed));

    static_assert(sizeof(SceneRecord) == sizeof(SceneHeader) + SCENE_MAX_LAYERS * sizeof(SceneLayer),
                  "SceneRecord must be SceneHeader immediately followed by layers (no padding)");
}  // namespace Lightnet
