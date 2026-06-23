#pragma once

#include <stdint.h>
#include "../../Common/Database/SingleRecordStore.hpp"
#include "Store/MqttConfigCodec.hpp"

namespace Lightnet {
    class MqttConfigStore
    {
        public:
            MqttConfigStore();

            void load();
            void flush();

            const MqttConfigRecord& record() const
            {
                return _record;
            }

            bool enabled() const
            {
                return _record.enabled != 0;
            }

            const char * broker() const
            {
                return _record.broker;
            }

            uint16_t port() const
            {
                return _record.port;
            }

            const char * username() const
            {
                return _record.username;
            }

            const char * password() const
            {
                return _record.password;
            }

            const char * topicPrefix() const
            {
                return _record.topicPrefix;
            }

            const char * discoveryPrefix() const
            {
                return _record.discoveryPrefix;
            }

            uint8_t brokerDiscovery() const
            {
                return _record.brokerDiscovery;
            }

            bool setEnabled(bool value);
            bool setBroker(const char *value);
            bool setPort(uint16_t value);
            bool setUsername(const char *value);
            bool setPassword(const char *value);
            bool setTopicPrefix(const char *value);
            bool setDiscoveryPrefix(const char *value);
            bool setBrokerDiscovery(uint8_t value);

            bool needsBrokerHost() const;

            void applyDefaults(const char *deviceTopicPrefix);

        private:
            static constexpr const char *MQTT_DATABASE_PATH = "/config/mqtt.db";
            static constexpr const char *MQTT_DATA_DIR      = "/config";

            MqttConfigRecord _record{};
            SingleRecordStore<MqttConfigCodec> _store{
                MQTT_DATABASE_PATH, MQTT_DATA_DIR
            };

            void writeFile();
            static void copyString(char *dest, size_t destLen, const char *src);
    };
}  // namespace Lightnet
