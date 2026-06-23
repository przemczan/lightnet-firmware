#include "MqttConfigCodec.hpp"
#include <string.h>

namespace Lightnet {
    static bool discoveryModeIsValid(uint8_t mode)
    {
        return mode <= MQTT_BROKER_DISCOVERY_HA_HOST;
    }

    static bool recordIsValid(const MqttConfigRecord& record)
    {
        if (record.enabled > 1) return false;

        if (!discoveryModeIsValid(record.brokerDiscovery)) return false;

        if (record.port == 0) return false;

        if (record.enabled
            && record.brokerDiscovery == MQTT_BROKER_DISCOVERY_MANUAL
            && record.broker[0] == '\0') {
            return false;
        }

        return true;
    }

    static void nullTerminateStrings(MqttConfigRecord& record)
    {
        record.broker[MQTT_BROKER_MAX - 1]            = '\0';
        record.username[MQTT_USER_MAX - 1]            = '\0';
        record.password[MQTT_PASSWORD_MAX - 1]        = '\0';
        record.topicPrefix[MQTT_TOPIC_PREFIX_MAX - 1] = '\0';
        record.discoveryPrefix[MQTT_DISCOVERY_MAX - 1] = '\0';
    }

    uint8_t MqttConfigCodec::serialize(const MqttConfigRecord& record, uint8_t *buffer, size_t capacity)
    {
        if (capacity < RECORD_SIZE) return MQTT_CONFIG_CODEC_BUF_TOO_SMALL;

        MqttConfigRecord copy = record;

        nullTerminateStrings(copy);

        if (!recordIsValid(copy)) return MQTT_CONFIG_CODEC_INVALID;

        memset(buffer, 0, capacity);
        memcpy(buffer, &copy, RECORD_SIZE);

        return MQTT_CONFIG_CODEC_OK;
    }

    uint8_t MqttConfigCodec::deserialize(const uint8_t *buffer, size_t length, MqttConfigRecord& out)
    {
        if (length < RECORD_SIZE) return MQTT_CONFIG_CODEC_BUF_TOO_SMALL;

        memcpy(&out, buffer, RECORD_SIZE);
        nullTerminateStrings(out);

        if (!recordIsValid(out)) return MQTT_CONFIG_CODEC_INVALID;

        return MQTT_CONFIG_CODEC_OK;
    }
}  // namespace Lightnet
