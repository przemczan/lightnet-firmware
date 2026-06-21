#pragma once

#include <ESPAsyncWebServer.h>
#include "../../AppState/AppStateStore.hpp"
#include "../../Panels/PanelsController.hpp"
#include "../../Scenes/ScenesService.hpp"
#include "../../../Core/Controller/AnimationScheduler.hpp"
#include "../../Appearance/AppearanceService.hpp"
#include "../../../Utils/MainLoopQueue.hpp"

class PacketMirror;  // forward declaration — only a pointer is stored

namespace Lightnet {
    class StateServer
    {
        public:
            StateServer(
                AsyncWebServer&     server,
                AppStateStore&      appState,
                PanelsController&   panelsController,
                ScenesService&      animService,
                AnimationScheduler& animScheduler,
                AppearanceService&    appearance,
                MainLoopQueue&      queue,
                PacketMirror *      packetMirror = nullptr
            );

            void begin();

        private:
            AsyncWebServer& server;
            AppStateStore& appState;
            PanelsController& panelsController;
            ScenesService& animService;
            AnimationScheduler& animScheduler;
            AppearanceService& appearance;
            MainLoopQueue& queue;
            PacketMirror *packetMirror;

            void registerRoutes();

            void handleGetState(AsyncWebServerRequest *req);
            void handlePostPower(AsyncWebServerRequest *req, const uint8_t *body, size_t len);

            // Emits the on/off side effects (panel packets, scene stop/resume). Runs on
            // the main loop; the caller has already flipped appState via setIsOn().
            void applyPowerEffects(bool on);
    };
}  // namespace Lightnet
