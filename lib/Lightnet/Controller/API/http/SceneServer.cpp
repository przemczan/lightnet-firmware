#include "SceneServer.hpp"
#include "HttpHelpers.hpp"
#include "../../../Utils/SimpleJson.hpp"
#include <Arduino.h>
#include "../../../Utils/Fs/Fs.hpp"
#include <string.h>
#include <stdio.h>
#include <new>

namespace Lightnet {
    SceneServer::SceneServer(
        AsyncWebServer&   _server,
        ScenePlayer&      _player,
        AnimationService& _animService,
        AppStateStore&    _appState,
        AppearanceStore&  _appearance,
        MainLoopQueue&    _queue
    )
        : server(_server), player(_player), animService(_animService), appState(_appState),
        appearance(_appearance), queue(_queue)
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
        server.on("/api/scenes/stop", HTTP_POST, [this](AsyncWebServerRequest *r) {
            handlePostStopScene(r);
        });
        Http::onBody(server, "/api/scenes/speed", HTTP_POST, Http::MAX_BODY_LARGE,
                     this, &SceneServer::handlePostSetSpeed);
        Http::onBody(server, "/api/scenes/play/one-shot", HTTP_POST, Http::MAX_BODY_LARGE,
                     this, &SceneServer::handlePostPlayOneShotScene);
        // No-body replay of lastPlayedScene; must be registered before the
        // /api/scenes/* wildcard POST handler, which would otherwise treat
        // "play" as a scene name.
        server.on("/api/scenes/play", HTTP_POST, [this](AsyncWebServerRequest *r) {
            handlePostPlayLastScene(r);
        });
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
        FsDir d("/scenes/");

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

            if (SceneStore::isHiddenName(name)) continue;

            n += snprintf(buf + n, sizeof(buf) - n, "%s{\"name\":\"%s\",\"size\":%u}",
                          first ? "" : ",", name, (unsigned)d.fileSize());
            first = false;
        }

        snprintf(buf + n, sizeof(buf) - n, "]");
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

        if (!Fs::exists(path)) {
            Http::sendError(req, 404, "not_found");

            return;
        }

        req->send(Fs::raw(), path, "application/json");
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

        if (!Fs::exists(path)) {
            Http::sendError(req, 404, "not_found");

            return;
        }

        Fs::remove(path);
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

    // Plays an already-parsed scene on the main loop (where I2C packet emission must
    // originate). `parsed` is heap-owned and freed by the task; if the queue is full we
    // free it here and report 503 so nothing leaks.
    void SceneServer::deferPlay(AsyncWebServerRequest *req, SceneParseResult *parsed)
    {
        struct Args {
            SceneServer *     self;
            SceneParseResult *parsed;
        } args { this, parsed };

        bool queued = queue.post(+[](const uint8_t *a, uint16_t) {
            Args x;

            memcpy(&x, a, sizeof(x));
            x.self->animService.playParsed(*x.parsed,
                                           x.self->appearance.paletteName(),
                                           x.self->appearance.baseColors());
            delete x.parsed;
        }, &args, sizeof(args));

        if (!queued) {
            delete parsed;
            Http::sendError(req, 503, "busy");

            return;
        }

        Http::sendAccepted(req);
    }

    void SceneServer::handlePostPlayOneShotScene(AsyncWebServerRequest *req, const uint8_t *body, size_t len)
    {
        if (!appState.isOn()) {
            Http::sendError(req, 409, "system_off");

            return;
        }

        // Heap (not the AsyncTCP stack): SceneParseResult is ~2.5 KB. Ownership passes to
        // the deferred play task.
        SceneParseResult *parsed = new (std::nothrow) SceneParseResult;

        if (!parsed) {
            Http::sendError(req, 500, "oom");

            return;
        }

        SceneResult r = animService.prepareInline((const char *)body, len, *parsed);

        if (!r.ok()) {
            delete parsed;
            sendSceneError(req, r);

            return;
        }

        appState.setLastPlayedScene(parsed->name, false);
        deferPlay(req, parsed);
    }

    void SceneServer::handlePostPlayLastScene(AsyncWebServerRequest *req)
    {
        if (!appState.isOn()) {
            Http::sendError(req, 409, "system_off");

            return;
        }

        const char *lastName = appState.lastPlayedScene();

        if (lastName[0] == '\0') {
            Http::sendError(req, 404, "no_last_played_scene");

            return;
        }

        const char *name = appState.lastPlayedSceneIsStored() ? lastName : SceneStore::oneShotName();

        SceneParseResult *parsed = new (std::nothrow) SceneParseResult;

        if (!parsed) {
            Http::sendError(req, 500, "oom");

            return;
        }

        SceneResult r = animService.prepareByName(name, *parsed);

        if (!r.ok()) {
            delete parsed;
            sendSceneError(req, r);

            return;
        }

        deferPlay(req, parsed);
    }

    void SceneServer::handlePostPlaySceneByName(AsyncWebServerRequest *req)
    {
        if (!appState.isOn()) {
            Http::sendError(req, 409, "system_off");

            return;
        }

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

        SceneParseResult *parsed = new (std::nothrow) SceneParseResult;

        if (!parsed) {
            Http::sendError(req, 500, "oom");

            return;
        }

        SceneResult r = animService.prepareByName(name, *parsed);

        if (!r.ok()) {
            delete parsed;
            sendSceneError(req, r);

            return;
        }

        appState.setLastPlayedScene(name, true);
        deferPlay(req, parsed);
    }

    void SceneServer::handlePostStopScene(AsyncWebServerRequest *req)
    {
        struct Args {
            SceneServer *self;
        } args { this };

        bool queued = queue.post(+[](const uint8_t *a, uint16_t) {
            Args x;

            memcpy(&x, a, sizeof(x));
            x.self->animService.stopScene();
        }, &args, sizeof(args));

        if (!queued) {
            Http::sendError(req, 503, "busy");

            return;
        }

        Http::sendAccepted(req);
    }

    void SceneServer::handlePostSetSpeed(AsyncWebServerRequest *req, const uint8_t *body, size_t len)
    {
        // Body: {"speed": <float 0.1-10.0>}
        const char *p   = (const char *)body;
        const char *end = p + len;

        if (!jsonEnterObject(p, end)) {
            Http::sendError(req, 400, "expected_object");

            return;
        }

        float speed = 1.0f;
        bool found = false;
        char key[12];

        while (jsonNextKey(p, end, key, sizeof(key))) {
            if (strcmp(key, "speed") == 0) {
                if (!jsonReadFloat(p, end, &speed)) {
                    Http::sendError(req, 400, "speed: not a number");

                    return;
                }

                found = true;
            } else {
                jsonSkipValue(p, end);
            }
        }

        if (!found) {
            Http::sendError(req, 400, "speed: required");

            return;
        }

        if (speed < 0.1f || speed > 10.0f) {
            Http::sendError(req, 422, "speed: must be 0.1-10.0");

            return;
        }

        struct Args {
            SceneServer *self;
            float        speed;
        } args { this, speed };

        bool queued = queue.post(+[](const uint8_t *a, uint16_t) {
            Args x;

            memcpy(&x, a, sizeof(x));
            x.self->animService.setSceneSpeed(x.speed);
        }, &args, sizeof(args));

        if (!queued) {
            Http::sendError(req, 503, "busy");

            return;
        }

        char buf[48];

        snprintf(buf, sizeof(buf), "{\"ok\":true,\"speed\":%.1f}", (double)speed);
        Http::sendAcceptedJson(req, buf);
    }
}  // namespace Lightnet
