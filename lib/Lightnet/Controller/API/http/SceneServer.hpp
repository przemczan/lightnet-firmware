#pragma once

#include <ESPAsyncWebServer.h>
#include "../../../Core/Controller/ScenePlayer.hpp"
#include "../../Scenes/AnimationService.hpp"
#include "../../Scenes/ISceneRepository.hpp"
#include "../../Scenes/LittleFsSceneRepository.hpp"
#include "../../AppState/AppStateStore.hpp"
#include "../../Appearance/AppearanceStore.hpp"
#include "../../../Utils/MainLoopQueue.hpp"

namespace Lightnet {
    class SceneServer
    {
        public:
            SceneServer(
                AsyncWebServer&   server,
                ISceneRepository& scenes,
                ScenePlayer&      player,
                AnimationService& animService,
                AppStateStore&    appState,
                AppearanceStore&  appearance,
                MainLoopQueue&    queue
            );

            void begin();

        private:
            AsyncWebServer& server;
            ISceneRepository& scenes;
            ScenePlayer& player;
            AnimationService& animService;
            AppStateStore& appState;
            AppearanceStore& appearance;
            MainLoopQueue& queue;

            void registerRoutes();
            void deferPlay(AsyncWebServerRequest *req, SceneParseResult *parsed);

            void handleListScenes(AsyncWebServerRequest *req);
            void handleGetSceneById(AsyncWebServerRequest *req);
            void handleDeleteScene(AsyncWebServerRequest *req);
            void handlePostSaveScene(AsyncWebServerRequest *req, const uint8_t *body, size_t len);
            void handlePostPlayOneShotScene(AsyncWebServerRequest *req, const uint8_t *body, size_t len);
            void handlePostPlayLastScene(AsyncWebServerRequest *req);
            void handlePostPlaySceneById(AsyncWebServerRequest *req);
            void handlePostStopScene(AsyncWebServerRequest *req);
            void handlePostSetSpeed(AsyncWebServerRequest *req, const uint8_t *body, size_t len);

            static void sendSceneError(AsyncWebServerRequest *req, const SceneResult& r);
            static int sceneErrorCode(SceneError e);
    };
}  // namespace Lightnet
