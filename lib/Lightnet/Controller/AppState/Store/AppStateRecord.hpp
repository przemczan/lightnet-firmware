#pragma once

#include <stdint.h>
#include "../../../Utils/EntryId.hpp"

namespace Lightnet {
    // Last-known runtime state restored on boot (one record per controller).
    struct AppStateRecord {
        uint8_t isOn;
        char    lastPlayedSceneId[ENTRY_ID_MAX + 1];
        uint8_t lastPlayedSceneIsStored;
    } __attribute__((packed));
}  // namespace Lightnet
