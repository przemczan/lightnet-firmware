#pragma once

#include <ESPAsyncWebServer.h>
#include "../../Appearance/AppearanceStore.hpp"
#include "../../Palettes/IPaletteRepository.hpp"
#include "../../Scenes/ScenesService.hpp"
#include "../../../Utils/MainLoopQueue.hpp"

namespace Lightnet {
    class AppearanceServer
    {
        public:
            AppearanceServer(
                AsyncWebServer&     server,
                AppearanceStore&    appearance,
                IPaletteRepository& palettes,
                ScenesService&      animService,
                MainLoopQueue&      queue
            );

            void begin();

        private:
            AsyncWebServer& server;
            AppearanceStore& appearance;
            IPaletteRepository& palettes;
            ScenesService& animService;
            MainLoopQueue& queue;

            void registerRoutes();

            void handleGetAppearance(AsyncWebServerRequest *req);
            void handlePatchAppearance(AsyncWebServerRequest *req, const uint8_t *body, size_t len);
    };
}  // namespace Lightnet
