#pragma once

#include <ESPAsyncWebServer.h>
#include "AppearanceStore.hpp"
#include "PaletteStore.hpp"

namespace Lightnet {

// Registers HTTP routes for appearance control:
//   GET/PUT /api/appearance       — full read/bulk-update
//   GET/PUT /api/brightness       — single field
//   GET/PUT /api/colors           — primary/secondary/tertiary
//   GET/PUT /api/palette          — currently selected palette name
//   GET    /api/palettes          — list available palette names (built-ins)
//
// JSON bodies are small and known-shape; AnimationServer hand-parses them rather
// than pulling in a full JSON library. The full Scene/Layer HTTP API from the
// design doc will land separately on top of this scaffolding.
class AnimationServer {
public:
    AnimationServer(AsyncWebServer& server, AppearanceStore& appearance, PaletteStore& palettes);

    void begin();

private:
    AsyncWebServer&   server;
    AppearanceStore&  appearance;
    PaletteStore&     palettes;

    void registerRoutes();

    // Endpoint handlers
    void handleGetAppearance(AsyncWebServerRequest* req);
    void handleGetBrightness(AsyncWebServerRequest* req);
    void handleGetColors(AsyncWebServerRequest* req);
    void handleGetPalette(AsyncWebServerRequest* req);
    void handleListPalettes(AsyncWebServerRequest* req);

    // PUT body parsers — called from the async body handler with the assembled buffer.
    void handlePutBrightness(AsyncWebServerRequest* req, const uint8_t* body, size_t len);
    void handlePutColors(AsyncWebServerRequest* req, const uint8_t* body, size_t len);
    void handlePutPalette(AsyncWebServerRequest* req, const uint8_t* body, size_t len);
    void handlePutAppearance(AsyncWebServerRequest* req, const uint8_t* body, size_t len);

    // Tiny per-request body buffer accumulator. Async server delivers PUT bodies in
    // chunks via the body callback; we collect into a temp buffer attached to the
    // request and parse on the final chunk.
    static constexpr size_t MAX_BODY = 512;
};

}  // namespace Lightnet
