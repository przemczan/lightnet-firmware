#pragma once

#include <ESPAsyncWebServer.h>
#include "../../AppState/AppStateStore.hpp"
#include "../../Panels/PanelsController.hpp"
#include "../../Scenes/AnimationService.hpp"
#include "../../Animations/AnimationScheduler.hpp"
#include "../../Appearance/AppearanceStore.hpp"

class PacketMirror;  // forward declaration — only a pointer is stored

namespace Lightnet {
    class StateServer
    {
        public:
            StateServer(
                AsyncWebServer&     server,
                AppStateStore&      appState,
                PanelsController&   panelsController,
                AnimationService&   animService,
                AnimationScheduler& animScheduler,
                AppearanceStore&    appearance,
                PacketMirror *      packetMirror = nullptr
            );

            void begin();

        private:
            AsyncWebServer& server;
            AppStateStore& appState;
            PanelsController& panelsController;
            AnimationService& animService;
            AnimationScheduler& animScheduler;
            AppearanceStore& appearance;
            PacketMirror *packetMirror;

            void registerRoutes();

            void handleGetPower(AsyncWebServerRequest *req);
            void handlePostPower(AsyncWebServerRequest *req, const uint8_t *body, size_t len);

            void applyIsOn(bool newValue);
    };
}  // namespace Lightnet
