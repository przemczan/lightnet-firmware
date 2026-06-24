#pragma once

#include <stddef.h>
#include <stdint.h>

namespace Lightnet {
    namespace HttpJson {
        static constexpr size_t CLOSE_RESERVE     = 2;
        static constexpr size_t LIST_MIN_CAPACITY = 128;

        // Per-element JSON size estimates for GET list endpoints.
        static constexpr size_t PALETTE_LIST_ENTRY = 384;
        static constexpr size_t SCENE_LIST_ENTRY   = 128;

        // Single-response JSON buffer sizes.
        static constexpr size_t PALETTE_GET_BUFFER           = 512;
        static constexpr size_t PALETTE_CREATE_BUFFER        = 48;
        static constexpr size_t SCENE_ID_BUFFER              = 48;
        static constexpr size_t SCENE_SPEED_BUFFER             = 48;
        static constexpr size_t APPEARANCE_GET_BUFFER        = 320;
        static constexpr size_t STATE_GET_BUFFER             = 256;
        static constexpr size_t STATE_GET_FIRMWARE_TAIL      = 96;
        static constexpr size_t STATE_POWER_BUFFER             = 16;
        static constexpr size_t MQTT_GET_BUFFER              = 768;
        static constexpr size_t CONFIGURATION_GET_BUFFER     = 64;
        static constexpr size_t CONFIGURATION_PATCH_BUFFER   = 48;

        inline size_t paletteListCapacity(uint16_t recordCount)
        {
            size_t cap = CLOSE_RESERVE + (size_t)recordCount * PALETTE_LIST_ENTRY;

            return (cap < LIST_MIN_CAPACITY) ? LIST_MIN_CAPACITY : cap;
        }

        inline size_t sceneListCapacity(uint16_t recordCount)
        {
            size_t cap = CLOSE_RESERVE + (size_t)recordCount * SCENE_LIST_ENTRY;

            return (cap < LIST_MIN_CAPACITY) ? LIST_MIN_CAPACITY : cap;
        }
    }  // namespace HttpJson
}  // namespace Lightnet
