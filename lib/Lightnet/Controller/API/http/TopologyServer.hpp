#pragma once

#include <ESPAsyncWebServer.h>
#include "../../Topology/TopologyConfigStore.hpp"
#include "../../Scenes/ScenePlayer.hpp"
#include "../../../Utils/MainLoopQueue.hpp"

namespace Lightnet {
    // HTTP surface for per-device topology config (logical root + panel tags).
    //   GET  /api/topology        → { logicalRoot, tags }
    //   PUT  /api/topology/root   → { "logicalRoot": N }   (persist + apply to playing scene)
    //   GET  /api/panel-tags      → { "1": ["accent"], … }
    //   PUT  /api/panel-tags      → whole-map replace (validated, persisted)
    class TopologyServer
    {
        public:
            TopologyServer(AsyncWebServer& server, TopologyConfigStore& store, ScenePlayer& player, MainLoopQueue& queue);

            void begin();

        private:
            AsyncWebServer& server;
            TopologyConfigStore& store;
            ScenePlayer& player;
            MainLoopQueue& queue;

            void registerRoutes();

            void handleGetTopology(AsyncWebServerRequest *req);
            void handleGetTags(AsyncWebServerRequest *req);
            void handlePutRoot(AsyncWebServerRequest *req, const uint8_t *body, size_t len);
            void handlePutTags(AsyncWebServerRequest *req, const uint8_t *body, size_t len);
    };
}  // namespace Lightnet
