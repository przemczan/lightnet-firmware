#pragma once

#include <ESPAsyncWebServer.h>
#include "../../Palettes/PaletteRepository.hpp"
#include "../../Appearance/AppearanceStore.hpp"

namespace Lightnet {
    class PaletteServer
    {
        public:
            PaletteServer(
                AsyncWebServer&    server,
                PaletteRepository& palettes,
                AppearanceStore&   appearance
            );

            void begin();

        private:
            AsyncWebServer& server;
            PaletteRepository& palettes;
            AppearanceStore& appearance;

            void registerRoutes();

            void handleListPalettes(AsyncWebServerRequest *req);
            void handleGetPalette(AsyncWebServerRequest *req);
            void handlePostPalette(AsyncWebServerRequest *req, const uint8_t *body, size_t len);
            void handlePutPalette(AsyncWebServerRequest *req, const uint8_t *body, size_t len);
            void handleDeletePalette(AsyncWebServerRequest *req);

            static int serializePaletteJson(const PaletteRecord& record, char *buf, size_t bufLen);
    };
}  // namespace Lightnet
