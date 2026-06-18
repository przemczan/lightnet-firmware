#pragma once

#include <ESPAsyncWebServer.h>
#include "../../Palettes/IPaletteRepository.hpp"
#include "../../Appearance/AppearanceStore.hpp"

namespace Lightnet {
    class PaletteServer
    {
        public:
            PaletteServer(
                AsyncWebServer&     server,
                IPaletteRepository& palettes,
                AppearanceStore&    appearance
            );

            void begin();

        private:
            AsyncWebServer& server;
            IPaletteRepository& palettes;
            AppearanceStore& appearance;

            void registerRoutes();

            void handleListPalettes(AsyncWebServerRequest *req);
            void handleGetPaletteById(AsyncWebServerRequest *req);
            void handlePostPalette(AsyncWebServerRequest *req, const uint8_t *body, size_t len);
            void handleDeletePalette(AsyncWebServerRequest *req);
    };
}  // namespace Lightnet
