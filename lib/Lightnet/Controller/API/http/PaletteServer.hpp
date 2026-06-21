#pragma once

#include <ESPAsyncWebServer.h>
#include "../../Palettes/PaletteRepository.hpp"
#include "../../Appearance/AppearanceService.hpp"

namespace Lightnet {
    class PaletteServer
    {
        public:
            PaletteServer(
                AsyncWebServer&    server,
                PaletteRepository& palettes,
                AppearanceService&   appearance
            );

            void begin();

            static int serializePaletteJson(const PaletteRecord& record, char *buf, size_t bufLen);

        private:
            AsyncWebServer& server;
            PaletteRepository& palettes;
            AppearanceService& appearance;

            void registerRoutes();

            void handleListPalettes(AsyncWebServerRequest *req);
            void handleGetPalette(AsyncWebServerRequest *req);
            void handlePostPalette(AsyncWebServerRequest *req, const uint8_t *body, size_t len);
            void handlePutPalette(AsyncWebServerRequest *req, const uint8_t *body, size_t len);
            void handleDeletePalette(AsyncWebServerRequest *req);
    };
}  // namespace Lightnet
