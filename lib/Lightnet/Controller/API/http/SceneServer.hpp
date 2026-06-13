#pragma once

#include <ESPAsyncWebServer.h>
#include "../../Scenes/ScenePlayer.hpp"
#include "../../Scenes/AnimationService.hpp"
#include "../../AppState/AppStateStore.hpp"
#include "../../Appearance/AppearanceStore.hpp"
#include "../../../Utils/MainLoopQueue.hpp"

namespace Lightnet {
    class SceneServer
    {
        public:
            SceneServer(
                AsyncWebServer&   server,
                ScenePlayer&      player,
                AnimationService& animService,
                AppStateStore&    appState,
                AppearanceStore&  appearance,
                MainLoopQueue&    queue
            );

            void begin();

        private:
            AsyncWebServer& server;
            ScenePlayer& player;
            AnimationService& animService;
            AppStateStore& appState;
            AppearanceStore& appearance;
            MainLoopQueue& queue;

            void registerRoutes();

            // Queue an already-parsed scene to be played on the main loop. Takes
            // ownership of `parsed` (freed by the task, or here if the queue is full).
            void deferPlay(AsyncWebServerRequest *req, SceneParseResult *parsed);

            void handleListScenes(AsyncWebServerRequest *req);
            void handleGetSceneByName(AsyncWebServerRequest *req);
            void handleDeleteScene(AsyncWebServerRequest *req);
            void handlePostSaveScene(AsyncWebServerRequest *req, const uint8_t *body, size_t len);
            void handlePostPlayOneShotScene(AsyncWebServerRequest *req, const uint8_t *body, size_t len);
            void handlePostPlayLastScene(AsyncWebServerRequest *req);
            void handlePostPlaySceneByName(AsyncWebServerRequest *req);
            void handlePostStopScene(AsyncWebServerRequest *req);
            void handlePostSetSpeed(AsyncWebServerRequest *req, const uint8_t *body, size_t len);

            static void sendSceneError(AsyncWebServerRequest *req, const SceneResult& r);
            static int sceneErrorCode(SceneError e);
    };
}  // namespace Lightnet
