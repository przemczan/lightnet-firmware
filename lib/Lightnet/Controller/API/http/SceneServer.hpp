#pragma once

#include <ESPAsyncWebServer.h>
#include "../../Scenes/ScenePlayer.hpp"
#include "../../Scenes/AnimationService.hpp"

namespace Lightnet {
    class SceneServer
    {
        public:
            SceneServer(
                AsyncWebServer&   server,
                ScenePlayer&      player,
                AnimationService& animService
            );

            void begin();

        private:
            AsyncWebServer& server;
            ScenePlayer& player;
            AnimationService& animService;

            void registerRoutes();

            void handleListScenes(AsyncWebServerRequest *req);
            void handleGetSceneStatus(AsyncWebServerRequest *req);
            void handleGetSceneByName(AsyncWebServerRequest *req);
            void handleDeleteScene(AsyncWebServerRequest *req);
            void handlePostSaveScene(AsyncWebServerRequest *req, const uint8_t *body, size_t len);
            void handlePostPlayScene(AsyncWebServerRequest *req, const uint8_t *body, size_t len);
            void handlePostPlaySceneByName(AsyncWebServerRequest *req);
            void handlePostStopScene(AsyncWebServerRequest *req);
            void handlePostSetSpeed(AsyncWebServerRequest *req, const uint8_t *body, size_t len);

            static void sendOk(AsyncWebServerRequest *req);
            static void sendOkJson(AsyncWebServerRequest *req, const char *json);
            static void sendError(AsyncWebServerRequest *req, int code, const char *msg);
            static void sendSceneError(AsyncWebServerRequest *req, const SceneResult& r);
            static int sceneErrorCode(SceneError e);

            static constexpr size_t MAX_BODY_LARGE = 4096;
    };
}  // namespace Lightnet
