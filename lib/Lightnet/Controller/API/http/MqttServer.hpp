#pragma once

#ifdef LIGHTNET_MQTT

    #include <ESPAsyncWebServer.h>
    #include "../../Mqtt/MqttConfigStore.hpp"
    #include "../../Mqtt/MqttService.hpp"
    #include "../../../Utils/MainLoopQueue.hpp"

    namespace Lightnet {
        class MqttServer
        {
            public:
                MqttServer(AsyncWebServer& server, MqttConfigStore& config, MqttService& mqtt);

                void begin();

            private:
                AsyncWebServer& server;
                MqttConfigStore& config;
                MqttService& mqtt;

                void registerRoutes();
                void handleGetMqtt(AsyncWebServerRequest *req);
                void handlePatchMqtt(AsyncWebServerRequest *req, const uint8_t *body, size_t len);
        };
    } // namespace Lightnet

#endif
