#pragma once

#include "MqttConfigRecord.hpp"
#include <stddef.h>
#include <stdint.h>

namespace Lightnet {
    enum MqttConfigCodecResult : uint8_t {
        MQTT_CONFIG_CODEC_OK            = 0,
        MQTT_CONFIG_CODEC_BUF_TOO_SMALL = 1,
        MQTT_CONFIG_CODEC_INVALID       = 2,
    };

    struct MqttConfigCodec {
        typedef MqttConfigRecord Model;

        static constexpr uint8_t MODEL_VERSION = 2;
        static constexpr size_t  RECORD_SIZE   = sizeof(MqttConfigRecord);
        static constexpr size_t  SCRATCH_SIZE  = RECORD_SIZE;

        static uint8_t serialize(const MqttConfigRecord& record, uint8_t *buffer, size_t capacity);
        static uint8_t deserialize(const uint8_t *buffer, size_t length, MqttConfigRecord& out);
    };
}  // namespace Lightnet
