#include "MqttConfigStore.hpp"
#include "../../Utils/Debug.hpp"
#include <Arduino.h>
#include <string.h>
#include <stdio.h>

namespace Lightnet {
    MqttConfigStore::MqttConfigStore()
    {
        _record.port            = MQTT_PORT_DEFAULT;
        _record.brokerDiscovery = MQTT_BROKER_DISCOVERY_AUTO;
        strncpy(_record.discoveryPrefix, "homeassistant", sizeof(_record.discoveryPrefix) - 1);
    }

    bool MqttConfigStore::needsBrokerHost() const
    {
        return (_record.enabled != 0)
               && (_record.brokerDiscovery == MQTT_BROKER_DISCOVERY_MANUAL);
    }

    bool MqttConfigStore::setBrokerDiscovery(uint8_t value)
    {
        if (value > MQTT_BROKER_DISCOVERY_HA_HOST) return false;

        if (value == _record.brokerDiscovery) return true;

        _record.brokerDiscovery = value;
        writeFile();

        return true;
    }

    void MqttConfigStore::load()
    {
        if (!_store.load(_record)) {
            D_PRINTLN("[MQTT] no valid record; writing defaults");
            _record.enabled         = 0;
            _record.port            = MQTT_PORT_DEFAULT;
            _record.brokerDiscovery = MQTT_BROKER_DISCOVERY_AUTO;
            _record.broker[0]       = '\0';
            _record.username[0]    = '\0';
            _record.password[0]    = '\0';
            _record.topicPrefix[0] = '\0';
            strncpy(_record.discoveryPrefix, "homeassistant", sizeof(_record.discoveryPrefix) - 1);
            writeFile();
        }
    }

    void MqttConfigStore::applyDefaults(const char *deviceTopicPrefix)
    {
        if (_record.topicPrefix[0] == '\0' && deviceTopicPrefix) {
            copyString(_record.topicPrefix, sizeof(_record.topicPrefix), deviceTopicPrefix);
        }

        if (_record.discoveryPrefix[0] == '\0') {
            strncpy(_record.discoveryPrefix, "homeassistant", sizeof(_record.discoveryPrefix) - 1);
        }
    }

    void MqttConfigStore::copyString(char *dest, size_t destLen, const char *src)
    {
        if (!dest || destLen == 0) return;

        if (!src) {
            dest[0] = '\0';

            return;
        }

        strncpy(dest, src, destLen - 1);
        dest[destLen - 1] = '\0';
    }

    bool MqttConfigStore::setEnabled(bool value)
    {
        uint8_t v = value ? 1 : 0;

        if (v == _record.enabled) return true;

        _record.enabled = v;
        writeFile();

        return true;
    }

    bool MqttConfigStore::setBroker(const char *value)
    {
        copyString(_record.broker, sizeof(_record.broker), value ? value : "");
        writeFile();

        return true;
    }

    bool MqttConfigStore::setPort(uint16_t value)
    {
        if (value == 0) return false;

        if (value == _record.port) return true;

        _record.port = value;
        writeFile();

        return true;
    }

    bool MqttConfigStore::setUsername(const char *value)
    {
        copyString(_record.username, sizeof(_record.username), value ? value : "");
        writeFile();

        return true;
    }

    bool MqttConfigStore::setPassword(const char *value)
    {
        copyString(_record.password, sizeof(_record.password), value ? value : "");
        writeFile();

        return true;
    }

    bool MqttConfigStore::setTopicPrefix(const char *value)
    {
        copyString(_record.topicPrefix, sizeof(_record.topicPrefix), value ? value : "");
        writeFile();

        return true;
    }

    bool MqttConfigStore::setDiscoveryPrefix(const char *value)
    {
        copyString(_record.discoveryPrefix, sizeof(_record.discoveryPrefix), value ? value : "");
        writeFile();

        return true;
    }

    void MqttConfigStore::flush()
    {
        writeFile();
    }

    void MqttConfigStore::writeFile()
    {
        if (!_store.save(_record)) {
            D_PRINTLN("[MQTT] failed to persist record");
        }
    }
}  // namespace Lightnet
