#pragma once
// AnimationServer registers HTTP routes and is the only HTTP-facing component.
// Responsibilities:
//   - Route registration
//   - Body accumulation (chunk assembly)
//   - Input parsing (SimpleJson) + basic range validation
//   - Delegating to AppearanceStore, PaletteStore, ScenePlayer, AnimationService
//   - Mapping SceneError → HTTP status code
//   - Sending JSON responses
//
// Business logic lives in the service classes, not here.

#include <ESPAsyncWebServer.h>
#include "../Appearance/AppearanceStore.hpp"
#include "../Appearance/PaletteStore.hpp"
#include "ScenePlayer.hpp"
#include "AnimationService.hpp"
#include "AnimationScheduler.hpp"

namespace Lightnet {

class AnimationServer {
public:
    AnimationServer(AsyncWebServer& server,
                    AppearanceStore& appearance,
                    PaletteStore& palettes,
                    ScenePlayer& player,
                    AnimationService& animService,
                    AnimationScheduler& scheduler);

    void begin();

private:
    AsyncWebServer&     server;
    AppearanceStore&    appearance;
    PaletteStore&       palettes;
    ScenePlayer&        player;
    AnimationService&   animService;
    AnimationScheduler& scheduler;

    void registerRoutes();

    // -- Appearance --
    void handleGetAppearance(AsyncWebServerRequest* req);
    void handleGetBrightness(AsyncWebServerRequest* req);
    void handleGetColors(AsyncWebServerRequest* req);
    void handleGetPalette(AsyncWebServerRequest* req);
    void handlePutAppearance(AsyncWebServerRequest* req, const uint8_t* body, size_t len);
    void handlePutBrightness(AsyncWebServerRequest* req, const uint8_t* body, size_t len);
    void handlePutColors(AsyncWebServerRequest* req, const uint8_t* body, size_t len);
    void handlePutPalette(AsyncWebServerRequest* req, const uint8_t* body, size_t len);

    // -- Palette CRUD --
    void handleListPalettes(AsyncWebServerRequest* req);
    void handleGetPaletteByName(AsyncWebServerRequest* req);
    void handlePostPalette(AsyncWebServerRequest* req, const uint8_t* body, size_t len);
    void handleDeletePalette(AsyncWebServerRequest* req);

    // -- Scene CRUD --
    void handleListScenes(AsyncWebServerRequest* req);
    void handleGetSceneStatus(AsyncWebServerRequest* req);
    void handleGetSceneByName(AsyncWebServerRequest* req);
    void handleDeleteScene(AsyncWebServerRequest* req);

    // -- Scene playback (delegate entirely to AnimationService) --
    void handlePostSaveScene(AsyncWebServerRequest* req, const uint8_t* body, size_t len);
    void handlePostPlayScene(AsyncWebServerRequest* req, const uint8_t* body, size_t len);
    void handlePostPlaySceneByName(AsyncWebServerRequest* req);
    void handlePostStopScene(AsyncWebServerRequest* req);

    // -- One-shot / trigger --
    void handleOneShotPlay(AsyncWebServerRequest* req, const uint8_t* body, size_t len);
    void handleAnimTrigger(AsyncWebServerRequest* req, const uint8_t* body, size_t len);

    // -- Response helpers --
    static void sendOk(AsyncWebServerRequest* req);
    static void sendOkJson(AsyncWebServerRequest* req, const char* json);
    static void sendError(AsyncWebServerRequest* req, int code, const char* msg);
    static void sendSceneError(AsyncWebServerRequest* req, const SceneResult& r);

    // Map SceneError → HTTP status code.
    static int sceneErrorCode(SceneError e);

    // Body buffer limits
    static constexpr size_t MAX_BODY_SMALL = 512;
    static constexpr size_t MAX_BODY_LARGE = 4096;
};

}  // namespace Lightnet
