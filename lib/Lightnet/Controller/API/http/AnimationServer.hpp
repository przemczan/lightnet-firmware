#pragma once

#include <ESPAsyncWebServer.h>
#include "../../Scenes/ScenesService.hpp"
#include "../../../Core/Controller/AnimationScheduler.hpp"
#include "../../Appearance/AppearanceService.hpp"
#include "../../AppState/AppStateStore.hpp"
#include "../../../Utils/MainLoopQueue.hpp"

namespace Lightnet {
    class AnimationServer
    {
        public:
            AnimationServer(
                AsyncWebServer&     server,
                ScenesService&      animService,
                AnimationScheduler& scheduler,
                AppearanceService&    appearance,
                AppStateStore&      appState,
                MainLoopQueue&      queue
            );

            void begin();

        private:
            AsyncWebServer& server;
            ScenesService& animService;
            AnimationScheduler& scheduler;
            AppearanceService& appearance;
            AppStateStore& appState;
            MainLoopQueue& queue;

            void registerRoutes();

            void handleOneShotPlay(AsyncWebServerRequest *req, const uint8_t *body, size_t len);
            void handleAnimTrigger(AsyncWebServerRequest *req, const uint8_t *body, size_t len);

            static void sendSceneError(AsyncWebServerRequest *req, const SceneResult& r);
            static int sceneErrorCode(SceneError e);
    };
}  // namespace Lightnet
