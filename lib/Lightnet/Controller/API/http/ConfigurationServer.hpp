#pragma once

#include <ESPAsyncWebServer.h>
#include "../../Configuration/ConfigurationStore.hpp"

namespace Lightnet {
    class ConfigurationServer
    {
        public:
            ConfigurationServer(AsyncWebServer& server, ConfigurationStore& config);

            void begin();

        private:
            AsyncWebServer& server;
            ConfigurationStore& config;

            void registerRoutes();

            void handleGetConfiguration(AsyncWebServerRequest *req);
            void handlePatchConfiguration(AsyncWebServerRequest *req, const uint8_t *body, size_t len);
    };
}  // namespace Lightnet
