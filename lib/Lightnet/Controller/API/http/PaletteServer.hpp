#pragma once

#include <ESPAsyncWebServer.h>
#include "../../Palettes/PaletteStore.hpp"
#include "../../Appearance/AppearanceStore.hpp"

namespace Lightnet {
    class PaletteServer
    {
        public:
            PaletteServer(
                AsyncWebServer&  server,
                PaletteStore&    palettes,
                AppearanceStore& appearance
            );

            void begin();

        private:
            AsyncWebServer& server;
            PaletteStore& palettes;
            AppearanceStore& appearance;

            void registerRoutes();

            void handleListPalettes(AsyncWebServerRequest *req);
            void handleGetPaletteByName(AsyncWebServerRequest *req);
            void handlePostPalette(AsyncWebServerRequest *req, const uint8_t *body, size_t len);
            void handleDeletePalette(AsyncWebServerRequest *req);

            static void sendOk(AsyncWebServerRequest *req);
            static void sendOkJson(AsyncWebServerRequest *req, const char *json);
            static void sendError(AsyncWebServerRequest *req, int code, const char *msg);

            static constexpr size_t MAX_BODY_LARGE = 4096;
    };
}  // namespace Lightnet
