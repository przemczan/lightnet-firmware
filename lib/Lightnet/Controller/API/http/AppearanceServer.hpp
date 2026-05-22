#pragma once

#include <ESPAsyncWebServer.h>
#include "../../Appearance/AppearanceStore.hpp"
#include "../../Palettes/PaletteStore.hpp"

namespace Lightnet {
    class AppearanceServer
    {
        public:
            AppearanceServer(
                AsyncWebServer&  server,
                AppearanceStore& appearance,
                PaletteStore&    palettes
            );

            void begin();

        private:
            AsyncWebServer& server;
            AppearanceStore& appearance;
            PaletteStore& palettes;

            void registerRoutes();

            void handleGetAppearance(AsyncWebServerRequest *req);
            void handleGetBrightness(AsyncWebServerRequest *req);
            void handleGetColors(AsyncWebServerRequest *req);
            void handleGetPalette(AsyncWebServerRequest *req);
            void handlePutAppearance(AsyncWebServerRequest *req, const uint8_t *body, size_t len);
            void handlePutBrightness(AsyncWebServerRequest *req, const uint8_t *body, size_t len);
            void handlePutColors(AsyncWebServerRequest *req, const uint8_t *body, size_t len);
            void handlePutPalette(AsyncWebServerRequest *req, const uint8_t *body, size_t len);

            static void sendOk(AsyncWebServerRequest *req);
            static void sendOkJson(AsyncWebServerRequest *req, const char *json);
            static void sendError(AsyncWebServerRequest *req, int code, const char *msg);

            static constexpr size_t MAX_BODY_SMALL = 512;
    };
}  // namespace Lightnet
