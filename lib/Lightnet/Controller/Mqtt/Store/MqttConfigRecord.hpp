#pragma once

#include <stdint.h>

namespace Lightnet {
    static constexpr uint8_t MQTT_BROKER_MAX       = 64;
    static constexpr uint8_t MQTT_USER_MAX         = 32;
    static constexpr uint8_t MQTT_PASSWORD_MAX     = 32;
    static constexpr uint8_t MQTT_TOPIC_PREFIX_MAX = 48;
    static constexpr uint8_t MQTT_DISCOVERY_MAX    = 48;
    static constexpr uint16_t MQTT_PORT_DEFAULT    = 1883;

    static constexpr uint8_t MQTT_BROKER_DISCOVERY_MANUAL    = 0;
    static constexpr uint8_t MQTT_BROKER_DISCOVERY_AUTO    = 1;
    static constexpr uint8_t MQTT_BROKER_DISCOVERY_MDNS    = 2;
    static constexpr uint8_t MQTT_BROKER_DISCOVERY_HA_HOST = 3;

    struct MqttConfigRecord {
        uint8_t  enabled;
        uint8_t  brokerDiscovery;
        uint16_t port;
        char     broker[MQTT_BROKER_MAX];
        char     username[MQTT_USER_MAX];
        char     password[MQTT_PASSWORD_MAX];
        char     topicPrefix[MQTT_TOPIC_PREFIX_MAX];
        char     discoveryPrefix[MQTT_DISCOVERY_MAX];
    } __attribute__((packed));
}  // namespace Lightnet
