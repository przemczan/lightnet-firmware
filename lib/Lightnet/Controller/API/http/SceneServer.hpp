#pragma once

#include <ESPAsyncWebServer.h>
#include "../../Scenes/ScenePlayer.hpp"
#include "../../Scenes/AnimationService.hpp"
#include "../../AppState/AppStateStore.hpp"
#include "../../Appearance/AppearanceStore.hpp"

namespace Lightnet {
    class SceneServer
    {
        public:
            SceneServer(
                AsyncWebServer&   server,
                ScenePlayer&      player,
                AnimationService& animService,
                AppStateStore&    appState,
                AppearanceStore&  appearance
            );

            void begin();

        private:
            AsyncWebServer& server;
            ScenePlayer& player;
            AnimationService& animService;
            AppStateStore& appState;
            AppearanceStore& appearance;

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

            static void sendSceneError(AsyncWebServerRequest *req, const SceneResult& r);
            static int sceneErrorCode(SceneError e);
    };
}  // namespace Lightnet
