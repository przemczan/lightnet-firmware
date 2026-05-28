#pragma once

#include <ESPAsyncWebServer.h>
#include "../../Scenes/AnimationService.hpp"
#include "../../Animations/AnimationScheduler.hpp"
#include "../../Appearance/AppearanceStore.hpp"

namespace Lightnet {
    class AnimationServer
    {
        public:
            AnimationServer(
                AsyncWebServer&     server,
                AnimationService&   animService,
                AnimationScheduler& scheduler,
                AppearanceStore&    appearance
            );

            void begin();

        private:
            AsyncWebServer& server;
            AnimationService& animService;
            AnimationScheduler& scheduler;
            AppearanceStore& appearance;

            void registerRoutes();

            void handleOneShotPlay(AsyncWebServerRequest *req, const uint8_t *body, size_t len);
            void handleAnimTrigger(AsyncWebServerRequest *req, const uint8_t *body, size_t len);

            static void sendOk(AsyncWebServerRequest *req);
            static void sendOkJson(AsyncWebServerRequest *req, const char *json);
            static void sendError(AsyncWebServerRequest *req, int code, const char *msg);
            static void sendSceneError(AsyncWebServerRequest *req, const SceneResult& r);
            static int sceneErrorCode(SceneError e);

            static constexpr size_t MAX_BODY_LARGE = 4096;
            static constexpr size_t MAX_BODY_SMALL = 512;
    };
}  // namespace Lightnet
