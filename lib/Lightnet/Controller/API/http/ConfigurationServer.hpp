#pragma once

#include <ESPAsyncWebServer.h>
#include "../../Configuration/ConfigurationStore.hpp"
#include "../../Topology/TopologyConfigStore.hpp"
#include "../../../Core/Controller/ScenePlayer.hpp"
#include "../../../Utils/MainLoopQueue.hpp"

namespace Lightnet {
    // Persistent device configuration:
    //   GET   /api/configuration → { powerStateOnBoot, logicalRoot }
    //   PATCH /api/configuration → partial update of any of the above fields
    class ConfigurationServer
    {
        public:
            ConfigurationServer(
                AsyncWebServer&      server,
                ConfigurationStore&  config,
                TopologyConfigStore& topology,
                ScenePlayer&         player,
                MainLoopQueue&       queue
            );

            void begin();

        private:
            AsyncWebServer& server;
            ConfigurationStore& config;
            TopologyConfigStore& topology;
            ScenePlayer& player;
            MainLoopQueue& queue;

            void registerRoutes();

            void handleGetConfiguration(AsyncWebServerRequest *req);
            void handlePatchConfiguration(AsyncWebServerRequest *req, const uint8_t *body, size_t len);
    };
}  // namespace Lightnet
