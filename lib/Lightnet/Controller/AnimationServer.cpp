#include "AnimationServer.hpp"
#include "../Utils/SimpleJson.hpp"
#include <Arduino.h>
#include <FS.h>
#ifdef ARDUINO_ARCH_ESP32
    #include <SPIFFS.h>
#endif
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

namespace Lightnet {

// ============================================================================
// Internal helpers (file-local)
// ============================================================================

namespace {

// True if `name` is safe for use in SPIFFS paths (no traversal, valid charset).
bool isSafeName(const char* name)
{
    if (!name || !name[0]) return false;
    for (const char* c = name; *c; c++) {
        if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') ||
              (*c >= '0' && *c <= '9') || *c == '_' || *c == '-')) return false;
    }
    // 18-char limit: /scenes/<name>.json = 8+18+5 = 31 chars = SPIFFS max path length.
    return strlen(name) <= 18;
}

// Extract the first path segment after `prefix` from a URL.
// e.g. nameFromUrl("/api/scenes/sunset/play", "/api/scenes/") → "sunset"
bool nameFromUrl(const char* url, const char* prefix, char* out, size_t outLen)
{
    size_t pfxLen = strlen(prefix);
    if (strncmp(url, prefix, pfxLen) != 0) return false;
    const char* start = url + pfxLen;
    const char* slash = strchr(start, '/');
    size_t len = slash ? (size_t)(slash - start) : strlen(start);
    if (len == 0 || len >= outLen) return false;
    memcpy(out, start, len);
    out[len] = '\0';
    return true;
}

// Body accumulator attached to each request via _tempObject.
struct BodyBuf { size_t len, cap; uint8_t data[1]; };

bool appendBody(AsyncWebServerRequest* req, const uint8_t* data, size_t len, size_t total, size_t maxCap)
{
    BodyBuf* buf = (BodyBuf*)req->_tempObject;
    if (!buf) {
        size_t cap = (total > 0) ? total : 256;
        if (cap > maxCap) cap = maxCap;
        buf = (BodyBuf*)malloc(sizeof(BodyBuf) + cap);
        if (!buf) return false;
        buf->len = 0; buf->cap = cap;
        req->_tempObject = buf;
        req->onDisconnect([req]() {
            if (req->_tempObject) { free(req->_tempObject); req->_tempObject = nullptr; }
        });
    }
    if (buf->len + len > buf->cap) return false;
    memcpy(buf->data + buf->len, data, len);
    buf->len += len;
    return true;
}

}  // anonymous namespace

// ============================================================================
// Constructor
// ============================================================================

AnimationServer::AnimationServer(AsyncWebServer& _server, AppearanceStore& _appearance,
                                  PaletteStore& _palettes, ScenePlayer& _player,
                                  AnimationService& _animService, AnimationScheduler& _scheduler)
    : server(_server), appearance(_appearance), palettes(_palettes),
      player(_player), animService(_animService), scheduler(_scheduler) {}

void AnimationServer::begin() { registerRoutes(); }

// ============================================================================
// Response helpers
// ============================================================================

void AnimationServer::sendOk(AsyncWebServerRequest* req)
{
    req->send(200, "application/json", "{\"ok\":true}");
}

void AnimationServer::sendOkJson(AsyncWebServerRequest* req, const char* json)
{
    req->send(200, "application/json", json);
}

void AnimationServer::sendError(AsyncWebServerRequest* req, int code, const char* msg)
{
    char buf[128];
    snprintf(buf, sizeof(buf), "{\"error\":\"%s\"}", msg ? msg : "error");
    req->send(code, "application/json", buf);
}

void AnimationServer::sendSceneError(AsyncWebServerRequest* req, const SceneResult& r)
{
    sendError(req, sceneErrorCode(r.err), r.msg);
}

int AnimationServer::sceneErrorCode(SceneError e)
{
    switch (e) {
        case SceneError::NotFound:     return 404;
        case SceneError::SchemaTooNew: return 409;
        case SceneError::IoFailure:    return 500;
        default:                       return 422;
    }
}

// ============================================================================
// Route registration
// ============================================================================

void AnimationServer::registerRoutes()
{
    // Body accumulators
    auto bodySmall = [this](AsyncWebServerRequest* r, uint8_t* d, size_t l, size_t i, size_t t) {
        if (!appendBody(r, d, l, t, MAX_BODY_SMALL)) sendError(r, 413, "body_too_large");
    };
    auto bodyLarge = [this](AsyncWebServerRequest* r, uint8_t* d, size_t l, size_t i, size_t t) {
        if (!appendBody(r, d, l, t, MAX_BODY_LARGE)) sendError(r, 413, "body_too_large");
    };

    // Call body handler on final chunk (request handler fires after all chunks).
    auto dispatchSmall = [this](void (AnimationServer::*m)(AsyncWebServerRequest*, const uint8_t*, size_t)) {
        return [this, m](AsyncWebServerRequest* r) {
            BodyBuf* buf = (BodyBuf*)r->_tempObject;
            if (!buf) { sendError(r, 400, "empty_body"); return; }
            (this->*m)(r, buf->data, buf->len);
        };
    };
    auto dispatchLarge = dispatchSmall;  // same pattern, different body size limit

    // -- Appearance --
    server.on("/api/appearance", HTTP_GET,  [this](AsyncWebServerRequest* r){ handleGetAppearance(r); });
    server.on("/api/brightness",  HTTP_GET,  [this](AsyncWebServerRequest* r){ handleGetBrightness(r); });
    server.on("/api/colors",      HTTP_GET,  [this](AsyncWebServerRequest* r){ handleGetColors(r); });
    server.on("/api/palette",     HTTP_GET,  [this](AsyncWebServerRequest* r){ handleGetPalette(r); });
    server.on("/api/appearance", HTTP_PUT,  dispatchSmall(&AnimationServer::handlePutAppearance),  nullptr, bodySmall);
    server.on("/api/brightness",  HTTP_PUT,  dispatchSmall(&AnimationServer::handlePutBrightness), nullptr, bodySmall);
    server.on("/api/colors",      HTTP_PUT,  dispatchSmall(&AnimationServer::handlePutColors),     nullptr, bodySmall);
    server.on("/api/palette",     HTTP_PUT,  dispatchSmall(&AnimationServer::handlePutPalette),    nullptr, bodySmall);

    // -- Palette CRUD (literal before wildcard) --
    server.on("/api/palettes",    HTTP_GET,    [this](AsyncWebServerRequest* r){ handleListPalettes(r); });
    server.on("/api/palettes",    HTTP_POST,   dispatchLarge(&AnimationServer::handlePostPalette),   nullptr, bodyLarge);
    server.on("/api/palettes/*",  HTTP_GET,    [this](AsyncWebServerRequest* r){ handleGetPaletteByName(r); });
    server.on("/api/palettes/*",  HTTP_DELETE, [this](AsyncWebServerRequest* r){ handleDeletePalette(r); });

    // -- Scene CRUD / playback (literals before wildcard) --
    server.on("/api/scenes",          HTTP_GET,  [this](AsyncWebServerRequest* r){ handleListScenes(r); });
    server.on("/api/scenes/status",   HTTP_GET,  [this](AsyncWebServerRequest* r){ handleGetSceneStatus(r); });
    server.on("/api/scenes/stop",     HTTP_POST, [this](AsyncWebServerRequest* r){ handlePostStopScene(r); });
    server.on("/api/scenes/play",     HTTP_POST, dispatchLarge(&AnimationServer::handlePostPlayScene), nullptr, bodyLarge);
    server.on("/api/scenes",          HTTP_POST, dispatchLarge(&AnimationServer::handlePostSaveScene), nullptr, bodyLarge);
    server.on("/api/scenes/*",        HTTP_GET,    [this](AsyncWebServerRequest* r){ handleGetSceneByName(r); });
    server.on("/api/scenes/*",        HTTP_DELETE, [this](AsyncWebServerRequest* r){ handleDeleteScene(r); });
    server.on("/api/scenes/*",        HTTP_POST,   [this](AsyncWebServerRequest* r){ handlePostPlaySceneByName(r); });

    // -- One-shot / trigger --
    server.on("/api/animations/play",    HTTP_POST, dispatchLarge(&AnimationServer::handleOneShotPlay), nullptr, bodyLarge);
    server.on("/api/animations/trigger", HTTP_POST, dispatchSmall(&AnimationServer::handleAnimTrigger), nullptr, bodySmall);
}

// ============================================================================
// Appearance — GET
// ============================================================================

void AnimationServer::handleGetAppearance(AsyncWebServerRequest* req)
{
    char h0[8], h1[8], h2[8];
    jsonFormatHex(appearance.baseColor(0).r, appearance.baseColor(0).g, appearance.baseColor(0).b, h0);
    jsonFormatHex(appearance.baseColor(1).r, appearance.baseColor(1).g, appearance.baseColor(1).b, h1);
    jsonFormatHex(appearance.baseColor(2).r, appearance.baseColor(2).g, appearance.baseColor(2).b, h2);
    char buf[256];
    snprintf(buf, sizeof(buf),
             "{\"brightness\":%u,\"baseColors\":[\"%s\",\"%s\",\"%s\"],\"palette\":\"%s\"}",
             (unsigned)appearance.brightness(), h0, h1, h2, appearance.paletteName());
    sendOkJson(req, buf);
}

void AnimationServer::handleGetBrightness(AsyncWebServerRequest* req)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "{\"value\":%u}", (unsigned)appearance.brightness());
    sendOkJson(req, buf);
}

void AnimationServer::handleGetColors(AsyncWebServerRequest* req)
{
    char h0[8], h1[8], h2[8];
    jsonFormatHex(appearance.baseColor(0).r, appearance.baseColor(0).g, appearance.baseColor(0).b, h0);
    jsonFormatHex(appearance.baseColor(1).r, appearance.baseColor(1).g, appearance.baseColor(1).b, h1);
    jsonFormatHex(appearance.baseColor(2).r, appearance.baseColor(2).g, appearance.baseColor(2).b, h2);
    char buf[128];
    snprintf(buf, sizeof(buf), "{\"primary\":\"%s\",\"secondary\":\"%s\",\"tertiary\":\"%s\"}", h0, h1, h2);
    sendOkJson(req, buf);
}

void AnimationServer::handleGetPalette(AsyncWebServerRequest* req)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"palette\":\"%s\"}", appearance.paletteName());
    sendOkJson(req, buf);
}

// ============================================================================
// Appearance — PUT
// ============================================================================

void AnimationServer::handlePutBrightness(AsyncWebServerRequest* req, const uint8_t* body, size_t len)
{
    SimpleJson j(body, len);
    long v = j.getInt("value");
    if (v < 0 || v > 255) { sendError(req, 422, "value_out_of_range"); return; }
    appearance.setBrightness((uint8_t)v);
    sendOk(req);
}

void AnimationServer::handlePutColors(AsyncWebServerRequest* req, const uint8_t* body, size_t len)
{
    SimpleJson j(body, len);
    const char* slotNames[BASE_COLORS_COUNT] = {"primary", "secondary", "tertiary"};
    Protocol::ColorRGB newColors[BASE_COLORS_COUNT];
    bool touched[BASE_COLORS_COUNT] = {false, false, false};

    for (uint8_t i = 0; i < BASE_COLORS_COUNT; i++) {
        char hex[16];
        if (!j.getString(slotNames[i], hex, sizeof(hex))) continue;
        uint8_t r, g, b;
        if (!jsonParseHexColor(hex, strlen(hex), &r, &g, &b)) {
            sendError(req, 422, "bad_hex_color"); return;
        }
        newColors[i] = {r, g, b};
        touched[i] = true;
    }

    // Build the merged set and apply atomically.
    Protocol::ColorRGB all[BASE_COLORS_COUNT];
    bool any = false;
    for (uint8_t i = 0; i < BASE_COLORS_COUNT; i++) {
        all[i] = touched[i] ? newColors[i] : appearance.baseColor(i);
        if (touched[i]) any = true;
    }
    if (any) appearance.setAllBaseColors(all);
    sendOk(req);
}

void AnimationServer::handlePutPalette(AsyncWebServerRequest* req, const uint8_t* body, size_t len)
{
    SimpleJson j(body, len);
    char name[20];
    if (!j.getString("palette", name, sizeof(name))) { sendError(req, 422, "missing_palette"); return; }
    if (!appearance.setPalette(name))                 { sendError(req, 404, "unknown_palette"); return; }
    sendOk(req);
}

void AnimationServer::handlePutAppearance(AsyncWebServerRequest* req, const uint8_t* body, size_t len)
{
    SimpleJson j(body, len);

    // brightness (optional)
    if (j.hasKey("brightness")) {
        long v = j.getInt("brightness");
        if (v < 0 || v > 255) { sendError(req, 422, "brightness_out_of_range"); return; }
        appearance.setBrightness((uint8_t)v);
    }

    // baseColors array (optional — full replacement)
    const char* p = j.rawValue("baseColors");
    if (p && jsonEnterArray(p, j.end())) {
        Protocol::ColorRGB cur[BASE_COLORS_COUNT];
        uint8_t got = 0;
        char hex[16];
        while (got < BASE_COLORS_COUNT && jsonNextElement(p, j.end())) {
            if (!jsonReadString(p, j.end(), hex, sizeof(hex))) break;
            uint8_t r, g, b;
            if (jsonParseHexColor(hex, strlen(hex), &r, &g, &b)) cur[got++] = {r, g, b};
        }
        if (got == BASE_COLORS_COUNT) appearance.setAllBaseColors(cur);
    }

    // palette (optional)
    char palName[20];
    if (j.getString("palette", palName, sizeof(palName))) {
        if (!appearance.setPalette(palName)) { sendError(req, 404, "unknown_palette"); return; }
    }

    sendOk(req);
}

// ============================================================================
// Palette CRUD
// ============================================================================

void AnimationServer::handleListPalettes(AsyncWebServerRequest* req)
{
    char buf[512];
    int n = snprintf(buf, sizeof(buf), "[\"userColors\"");
    for (uint8_t i = 0; i < palettes.builtInCount() && n + 32 < (int)sizeof(buf); i++) {
        n += snprintf(buf + n, sizeof(buf) - n, ",\"%s\"", palettes.builtInName(i));
    }
    Dir d = SPIFFS.openDir("/palettes/");
    while (d.next() && n + 32 < (int)sizeof(buf)) {
        String fn = d.fileName();
        const char* base = fn.c_str();
        if (strncmp(base, "/palettes/", 10) == 0) base += 10;
        size_t blen = strlen(base);
        if (blen > 5 && strcmp(base + blen - 5, ".json") == 0) {
            char name[24] = {0}; size_t nlen = blen - 5;
            if (nlen < sizeof(name) && !palettes.isBuiltIn(name)) {
                memcpy(name, base, nlen);
                n += snprintf(buf + n, sizeof(buf) - n, ",\"%s\"", name);
            }
        }
    }
    snprintf(buf + n, sizeof(buf) - n, "]");
    sendOkJson(req, buf);
}

void AnimationServer::handleGetPaletteByName(AsyncWebServerRequest* req)
{
    char name[24];
    if (!nameFromUrl(req->url().c_str(), "/api/palettes/", name, sizeof(name)) || !isSafeName(name)) {
        sendError(req, 400, "invalid_name"); return;
    }
    GradientStop stops[PALETTE_STOPS]; uint8_t count = 0;
    if (strcmp(name, "userColors") == 0) {
        PaletteStore::buildUserColors(appearance.baseColors(), stops, count);
    } else if (!palettes.resolve(name, stops, count)) {
        sendError(req, 404, "not_found"); return;
    }
    char buf[512]; int n = snprintf(buf, sizeof(buf), "{\"schemaVersion\":1,\"name\":\"%s\",\"stops\":[", name);
    for (uint8_t i = 0; i < count && n + 32 < (int)sizeof(buf); i++) {
        char hex[8]; jsonFormatHex(stops[i].r, stops[i].g, stops[i].b, hex);
        n += snprintf(buf + n, sizeof(buf) - n, "%s[%u,\"%s\"]", i ? "," : "", (unsigned)stops[i].pos, hex);
    }
    snprintf(buf + n, sizeof(buf) - n, "]}");
    sendOkJson(req, buf);
}

void AnimationServer::handlePostPalette(AsyncWebServerRequest* req, const uint8_t* body, size_t len)
{
    GradientStop stops[PALETTE_STOPS]; uint8_t count = 0;
    if (!PaletteStore::parsePaletteJson((const char*)body, len, stops, count)) {
        sendError(req, 422, "invalid_palette_json"); return;
    }
    SimpleJson j(body, len);
    char name[20];
    if (!j.getString("name", name, sizeof(name)) || !isSafeName(name)) {
        sendError(req, 422, "missing_or_invalid_name"); return;
    }
    if (palettes.isBuiltIn(name) || strcmp(name, "userColors") == 0) {
        sendError(req, 403, "cannot_overwrite_builtin"); return;
    }
    if (!palettes.save(name, stops, count)) {
        sendError(req, 500, "spiffs_write_failed"); return;
    }
    char resp[48]; snprintf(resp, sizeof(resp), "{\"saved\":\"%s\"}", name);
    sendOkJson(req, resp);
}

void AnimationServer::handleDeletePalette(AsyncWebServerRequest* req)
{
    char name[24];
    if (!nameFromUrl(req->url().c_str(), "/api/palettes/", name, sizeof(name)) || !isSafeName(name)) {
        sendError(req, 400, "invalid_name"); return;
    }
    if (palettes.isBuiltIn(name) || strcmp(name, "userColors") == 0) {
        sendError(req, 403, "cannot_delete_builtin"); return;
    }
    if (!palettes.deleteUserPalette(name)) { sendError(req, 404, "not_found"); return; }
    sendOk(req);
}

// ============================================================================
// Scene — list / get / status / delete
// ============================================================================

void AnimationServer::handleListScenes(AsyncWebServerRequest* req)
{
    char buf[512];
    // SceneStore is not held directly — build the list inline via SPIFFS.
    Dir d = SPIFFS.openDir("/scenes/"); int n = snprintf(buf, sizeof(buf), "["); bool first = true;
    while (d.next() && n + 64 < (int)sizeof(buf)) {
        String fn = d.fileName(); const char* base = fn.c_str();
        if (strncmp(base, "/scenes/", 8) == 0) base += 8;
        size_t blen = strlen(base);
        if (blen <= 5 || strcmp(base + blen - 5, ".json") != 0) continue;
        if (blen > 9 && strcmp(base + blen - 9, ".json.tmp") == 0) continue;
        char name[24] = {0}; size_t nlen = blen - 5; if (nlen >= sizeof(name)) continue;
        memcpy(name, base, nlen);
        n += snprintf(buf + n, sizeof(buf) - n, "%s{\"name\":\"%s\",\"size\":%u}",
                      first ? "" : ",", name, (unsigned)d.fileSize());
        first = false;
    }
    snprintf(buf + n, sizeof(buf) - n, "]");
    sendOkJson(req, buf);
}

void AnimationServer::handleGetSceneStatus(AsyncWebServerRequest* req)
{
    char buf[128];
    player.writeStatusJson(buf, sizeof(buf));
    sendOkJson(req, buf);
}

void AnimationServer::handleGetSceneByName(AsyncWebServerRequest* req)
{
    char name[24];
    if (!nameFromUrl(req->url().c_str(), "/api/scenes/", name, sizeof(name)) || !isSafeName(name)) {
        sendError(req, 400, "invalid_name"); return;
    }
    char path[36]; snprintf(path, sizeof(path), "/scenes/%s.json", name);
    if (!SPIFFS.exists(path)) { sendError(req, 404, "not_found"); return; }
    req->send(SPIFFS, path, "application/json");
}

void AnimationServer::handleDeleteScene(AsyncWebServerRequest* req)
{
    char name[24];
    if (!nameFromUrl(req->url().c_str(), "/api/scenes/", name, sizeof(name)) || !isSafeName(name)) {
        sendError(req, 400, "invalid_name"); return;
    }
    char path[36]; snprintf(path, sizeof(path), "/scenes/%s.json", name);
    if (!SPIFFS.exists(path)) { sendError(req, 404, "not_found"); return; }
    SPIFFS.remove(path);
    sendOk(req);
}

// ============================================================================
// Scene — save / play / stop  (all delegate to AnimationService)
// ============================================================================

void AnimationServer::handlePostSaveScene(AsyncWebServerRequest* req, const uint8_t* body, size_t len)
{
    auto r = animService.saveScene((const char*)body, len);
    if (!r.ok()) { sendSceneError(req, r); return; }
    sendOk(req);
}

void AnimationServer::handlePostPlayScene(AsyncWebServerRequest* req, const uint8_t* body, size_t len)
{
    auto r = animService.playSceneInline((const char*)body, len);
    if (!r.ok()) { sendSceneError(req, r); return; }
    sendOk(req);
}

void AnimationServer::handlePostPlaySceneByName(AsyncWebServerRequest* req)
{
    const char* url = req->url().c_str();
    // Distinguish /api/scenes/{name}/play from /api/scenes/{name}
    if (!strstr(url + strlen("/api/scenes/"), "/play")) {
        sendError(req, 404, "not_found"); return;
    }
    char name[24];
    if (!nameFromUrl(url, "/api/scenes/", name, sizeof(name)) || !isSafeName(name)) {
        sendError(req, 400, "invalid_name"); return;
    }
    auto r = animService.playSceneByName(name);
    if (!r.ok()) { sendSceneError(req, r); return; }
    sendOk(req);
}

void AnimationServer::handlePostStopScene(AsyncWebServerRequest* req)
{
    animService.stopScene();
    sendOk(req);
}

// ============================================================================
// One-shot / trigger
// ============================================================================

void AnimationServer::handleOneShotPlay(AsyncWebServerRequest* req, const uint8_t* body, size_t len)
{
    // Pass current appearance as defaults — the one-shot may not specify its own.
    auto r = animService.playOneShot((const char*)body, len,
                                      appearance.paletteName(), appearance.baseColors());
    if (!r.ok()) { sendSceneError(req, r); return; }
    sendOk(req);
}

void AnimationServer::handleAnimTrigger(AsyncWebServerRequest* req, const uint8_t* body, size_t len)
{
    SimpleJson j(body, len);
    long grp = j.getInt("group");
    long val = j.getInt("value");
    if (grp <= 0 || grp > 254) { sendError(req, 422, "group_out_of_range"); return; }
    if (val < 0) val = 200;
    if (val > 255) { sendError(req, 422, "value_out_of_range"); return; }
    scheduler.triggerGroup((uint8_t)grp, (uint8_t)val);
    sendOk(req);
}

}  // namespace Lightnet
