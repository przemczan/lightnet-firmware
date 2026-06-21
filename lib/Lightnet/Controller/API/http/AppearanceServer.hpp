#pragma once

#include <ESPAsyncWebServer.h>
#include "../../Appearance/AppearanceService.hpp"
#include "../../Palettes/PaletteRepository.hpp"
#include "../../Scenes/ScenesService.hpp"
#include "../../../Utils/MainLoopQueue.hpp"

namespace Lightnet {
    class AppearanceServer
    {
        public:
            AppearanceServer(
                AsyncWebServer&    server,
                AppearanceService&   appearance,
                PaletteRepository& palettes,
                ScenesService&     animService,
                MainLoopQueue&     queue
            );

            void begin();

        private:
            AsyncWebServer& server;
            AppearanceService& appearance;
            PaletteRepository& palettes;
            ScenesService& animService;
            MainLoopQueue& queue;

            void registerRoutes();

            void handleGetAppearance(AsyncWebServerRequest *req);
            void handlePatchAppearance(AsyncWebServerRequest *req, const uint8_t *body, size_t len);
    };
}  // namespace Lightnet
