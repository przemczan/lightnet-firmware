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

            static void sendOk(AsyncWebServerRequest *req);
            static void sendOkJson(AsyncWebServerRequest *req, const char *json);
            static void sendError(AsyncWebServerRequest *req, int code, const char *msg);

            static constexpr size_t MAX_BODY_SMALL = 64;
    };
}  // namespace Lightnet
