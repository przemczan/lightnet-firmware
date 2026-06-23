#pragma once

#ifdef LIGHTNET_MQTT

    #include "Store/MqttConfigRecord.hpp"
    #include <stdint.h>

    struct mdns_search_once_s;

    namespace Lightnet {
        enum class MqttBrokerDiscoverySource : uint8_t {
            None = 0,
            Manual,
            Mdns,
            HomeAssistant,
            Hassio,
        };

        enum class MqttBrokerDiscoveryState : uint8_t {
            Idle = 0,
            Searching,
            Done,
            Failed,
        };

        struct MqttBrokerEndpoint {
            char                      host[MQTT_BROKER_MAX];
            uint16_t                  port;
            MqttBrokerDiscoverySource source;
            bool                      valid;
        };

        class MqttBrokerDiscovery
        {
            public:
                MqttBrokerDiscovery();

                void start(uint8_t mode, const char *manualHost, uint16_t manualPort);
                void tick();
                void cancel();

                MqttBrokerDiscoveryState state() const
                {
                    return _state;
                }

                bool isFinished() const
                {
                    return (_state == MqttBrokerDiscoveryState::Done)
                           || (_state == MqttBrokerDiscoveryState::Failed);
                }

                const MqttBrokerEndpoint& result() const
                {
                    return _endpoint;
                }

                static const char * sourceName(MqttBrokerDiscoverySource source);

            private:
                enum class QueryStep : uint8_t {
                    None = 0,
                    MqttService,
                    HomeAssistant,
                    Hassio,
                };

                mdns_search_once_s *_search;
                QueryStep _step;
                uint8_t _mode;
                MqttBrokerDiscoveryState _state;
                MqttBrokerEndpoint _endpoint;

                void finishFailure();
                void finishSuccess(const char *host, uint16_t port, MqttBrokerDiscoverySource source);
                void beginStep(QueryStep step);
                void advanceChain();
                void pollSearch();
                void cancelSearch();
                bool allowsMdnsBrowse() const;
                bool allowsHaFallback() const;
        };
    } // namespace Lightnet

#endif
