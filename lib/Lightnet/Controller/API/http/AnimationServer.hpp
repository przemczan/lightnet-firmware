#pragma once

#include <ESPAsyncWebServer.h>
#include "../../Scenes/AnimationService.hpp"
#include "../../Animations/AnimationScheduler.hpp"
#include "../../Appearance/AppearanceStore.hpp"
#include "../../AppState/AppStateStore.hpp"

namespace Lightnet {
    class AnimationServer
    {
        public:
            AnimationServer(
                AsyncWebServer&     server,
                AnimationService&   animService,
                AnimationScheduler& scheduler,
                AppearanceStore&    appearance,
                AppStateStore&      appState
            );

            void begin();

        private:
            AsyncWebServer& server;
            AnimationService& animService;
            AnimationScheduler& scheduler;
            AppearanceStore& appearance;
            AppStateStore& appState;

            void registerRoutes();

            void handleOneShotPlay(AsyncWebServerRequest *req, const uint8_t *body, size_t len);
            void handleAnimTrigger(AsyncWebServerRequest *req, const uint8_t *body, size_t len);

            static void sendSceneError(AsyncWebServerRequest *req, const SceneResult& r);
            static int sceneErrorCode(SceneError e);
    };
}  // namespace Lightnet
