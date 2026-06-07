#pragma once

#include <ESPAsyncWebServer.h>
#include "../../Appearance/AppearanceStore.hpp"
#include "../../Palettes/PaletteStore.hpp"
#include "../../Scenes/AnimationService.hpp"

namespace Lightnet {
    class AppearanceServer
    {
        public:
            AppearanceServer(
                AsyncWebServer&   server,
                AppearanceStore&  appearance,
                PaletteStore&     palettes,
                AnimationService& animService
            );

            void begin();

        private:
            AsyncWebServer& server;
            AppearanceStore& appearance;
            PaletteStore& palettes;
            AnimationService& animService;

            void registerRoutes();

            void handleGetAppearance(AsyncWebServerRequest *req);
            void handlePatchAppearance(AsyncWebServerRequest *req, const uint8_t *body, size_t len);
    };
}  // namespace Lightnet
