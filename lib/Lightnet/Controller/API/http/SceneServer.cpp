#include "SceneServer.hpp"
#include "HttpHelpers.hpp"
#include "../../../Utils/SimpleJson.hpp"
#include <Arduino.h>
#include <FS.h>
#ifdef ARDUINO_ARCH_ESP32
    #include <SPIFFS.h>
#endif
#include <string.h>
#include <stdio.h>

namespace Lightnet {
    SceneServer::SceneServer(
        AsyncWebServer&   _server,
        ScenePlayer&      _player,
        AnimationService& _animService
    )
        : server(_server), player(_player), animService(_animService)
    {
    }

    void SceneServer::begin()
    {
        registerRoutes();
    }

    void SceneServer::sendSceneError(AsyncWebServerRequest *req, const SceneResult& r)
    {
        Http::sendError(req, sceneErrorCode(r.err), r.msg);
    }

    int SceneServer::sceneErrorCode(SceneError e)
    {
        switch (e) {
            case SceneError::NotFound:     return 404;
            case SceneError::SchemaTooNew: return 409;
            case SceneError::IoFailure:    return 500;
            default:                       return 422;
        }
    }

    void SceneServer::registerRoutes()
    {
        // Specific action routes before wildcard and general routes.
        server.on("/api/scenes/status", HTTP_GET, [this](AsyncWebServerRequest *r) {
            handleGetSceneStatus(r);
        });
        server.on("/api/scenes/stop", HTTP_POST, [this](AsyncWebServerRequest *r) {
            handlePostStopScene(r);
        });
        Http::onBody(server, "/api/scenes/speed", HTTP_POST, Http::MAX_BODY_LARGE,
                     this, &SceneServer::handlePostSetSpeed);
        Http::onBody(server, "/api/scenes/play", HTTP_POST, Http::MAX_BODY_LARGE,
                     this, &SceneServer::handlePostPlayScene);
        // Wildcard routes before general /api/scenes routes.
        server.on("/api/scenes/*", HTTP_GET, [this](AsyncWebServerRequest *r) {
            handleGetSceneByName(r);
        });
        server.on("/api/scenes/*", HTTP_DELETE, [this](AsyncWebServerRequest *r) {
            handleDeleteScene(r);
        });
        server.on("/api/scenes/*", HTTP_POST, [this](AsyncWebServerRequest *r) {
            handlePostPlaySceneByName(r);
        });
        // General routes last.
        server.on("/api/scenes", HTTP_GET, [this](AsyncWebServerRequest *r) {
            handleListScenes(r);
        });
        Http::onBody(server, "/api/scenes", HTTP_POST, Http::MAX_BODY_LARGE,
                     this, &SceneServer::handlePostSaveScene);
    }

    void SceneServer::handleListScenes(AsyncWebServerRequest *req)
    {
        char buf[512];
        Dir d = SPIFFS.openDir("/scenes/");

        int n = snprintf(buf, sizeof(buf), "[");
        bool first = true;

        while (d.next() && n + 64 < (int)sizeof(buf)) {
            String fn = d.fileName();
            const char *base = fn.c_str();

            if (strncmp(base, "/scenes/", 8) == 0) base += 8;

            size_t blen = strlen(base);

            if (blen <= 5 || strcmp(base + blen - 5, ".json") != 0) continue;

            if (blen > 9 && strcmp(base + blen - 9, ".json.tmp") == 0) continue;

            char name[24] = { 0 };
            size_t nlen = blen - 5;

            if (nlen >= sizeof(name)) continue;

            memcpy(name, base, nlen);
            n += snprintf(buf + n, sizeof(buf) - n, "%s{\"name\":\"%s\",\"size\":%u}",
                          first ? "" : ",", name, (unsigned)d.fileSize());
            first = false;
        }

        snprintf(buf + n, sizeof(buf) - n, "]");
        Http::sendOkJson(req, buf);
    }

    void SceneServer::handleGetSceneStatus(AsyncWebServerRequest *req)
    {
        char buf[128];

        player.writeStatusJson(buf, sizeof(buf));
        Http::sendOkJson(req, buf);
    }

    void SceneServer::handleGetSceneByName(AsyncWebServerRequest *req)
    {
        char name[24];

        if (!Http::nameFromUrl(req->url().c_str(), "/api/scenes/", name, sizeof(name)) ||
            !Http::isSafeName(name)) {
            Http::sendError(req, 400, "invalid_name");

            return;
        }

        char path[36];

        snprintf(path, sizeof(path), "/scenes/%s.json", name);

        if (!SPIFFS.exists(path)) {
            Http::sendError(req, 404, "not_found");

            return;
        }

        req->send(SPIFFS, path, "application/json");
    }

    void SceneServer::handleDeleteScene(AsyncWebServerRequest *req)
    {
        char name[24];

        if (!Http::nameFromUrl(req->url().c_str(), "/api/scenes/", name, sizeof(name)) ||
            !Http::isSafeName(name)) {
            Http::sendError(req, 400, "invalid_name");

            return;
        }

        char path[36];

        snprintf(path, sizeof(path), "/scenes/%s.json", name);

        if (!SPIFFS.exists(path)) {
            Http::sendError(req, 404, "not_found");

            return;
        }

        SPIFFS.remove(path);
        Http::sendOk(req);
    }

    void SceneServer::handlePostSaveScene(AsyncWebServerRequest *req, const uint8_t *body, size_t len)
    {
        auto r = animService.saveScene((const char *)body, len);

        if (!r.ok()) {
            sendSceneError(req, r);

            return;
        }

        Http::sendOk(req);
    }

    void SceneServer::handlePostPlayScene(AsyncWebServerRequest *req, const uint8_t *body, size_t len)
    {
        auto r = animService.playSceneInline((const char *)body, len);

        if (!r.ok()) {
            sendSceneError(req, r);

            return;
        }

        Http::sendOk(req);
    }

    void SceneServer::handlePostPlaySceneByName(AsyncWebServerRequest *req)
    {
        const char *url = req->url().c_str();

        if (!strstr(url + strlen("/api/scenes/"), "/play")) {
            Http::sendError(req, 404, "not_found");

            return;
        }

        char name[24];

        if (!Http::nameFromUrl(url, "/api/scenes/", name, sizeof(name)) || !Http::isSafeName(name)) {
            Http::sendError(req, 400, "invalid_name");

            return;
        }

        auto r = animService.playSceneByName(name);

        if (!r.ok()) {
            sendSceneError(req, r);

            return;
        }

        Http::sendOk(req);
    }

    void SceneServer::handlePostStopScene(AsyncWebServerRequest *req)
    {
        animService.stopScene();
        Http::sendOk(req);
    }

    void SceneServer::handlePostSetSpeed(AsyncWebServerRequest *req, const uint8_t *body, size_t len)
    {
        // Body: {"speed": <float 0.1-10.0>}
        const char *p   = (const char *)body;
        const char *end = p + len;

        if (!jsonEnterObject(p, end)) { Http::sendError(req, 400, "expected_object"); return; }

        float speed = 1.0f;
        bool found = false;
        char key[12];

        while (jsonNextKey(p, end, key, sizeof(key))) {
            if (strcmp(key, "speed") == 0) {
                if (!jsonReadFloat(p, end, &speed)) { Http::sendError(req, 400, "speed: not a number"); return; }

                found = true;
            } else {
                jsonSkipValue(p, end);
            }
        }

        if (!found) { Http::sendError(req, 400, "speed: required"); return; }

        if (speed < 0.1f || speed > 10.0f) { Http::sendError(req, 422, "speed: must be 0.1-10.0"); return; }

        animService.setSceneSpeed(speed);

        char buf[48];

        snprintf(buf, sizeof(buf), "{\"ok\":true,\"speed\":%.1f}", (double)speed);
        Http::sendOkJson(req, buf);
    }
}  // namespace Lightnet
