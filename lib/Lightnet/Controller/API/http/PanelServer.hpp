#pragma once

#include <ESPAsyncWebServer.h>
#include "../../Panels/PanelsController.hpp"

namespace Lightnet {
    class PanelServer
    {
        public:
            PanelServer(
                AsyncWebServer&   server,
                PanelsController& panelsController
            );

            void begin();

        private:
            AsyncWebServer&   server;
            PanelsController& panelsController;

            void registerRoutes();

            void handleGetPanels(AsyncWebServerRequest *req);
            void handleGetEdges(AsyncWebServerRequest *req);
            void handlePutPanel(AsyncWebServerRequest *req, const uint8_t *body, size_t len);
    };
}  // namespace Lightnet
