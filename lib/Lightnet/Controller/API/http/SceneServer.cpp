#include "SceneServer.hpp"
#include "HttpHelpers.hpp"
#include "../../../Utils/EntryId.hpp"
#include "../../Store/StoreStreamResponse.hpp"
#include "../../../Utils/SimpleJson.hpp"
#include <Arduino.h>
#include <string.h>
#include <stdio.h>
#include <new>

namespace Lightnet {
    SceneServer::SceneServer(
        AsyncWebServer&   _server,
        ISceneRepository& _scenes,
        ScenePlayer&      _player,
        ScenesService&    _animService,
        AppStateStore&    _appState,
        AppearanceStore&  _appearance,
        MainLoopQueue&    _queue
    )
        : server(_server), scenes(_scenes), player(_player), animService(_animService),
        appState(_appState), appearance(_appearance), queue(_queue)
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
            case SceneError::IdMismatch:   return 422;
            case SceneError::IoFailure:    return 500;
            default:                       return 422;
        }
    }

    void SceneServer::registerRoutes()
    {
        server.on("/api/scenes/stop", HTTP_POST, [this](AsyncWebServerRequest *r) {
            handlePostStopScene(r);
        });
        Http::onBody(server, "/api/scenes/speed", HTTP_POST, Http::MAX_BODY_LARGE,
                     this, &SceneServer::handlePostSetSpeed);
        Http::onBody(server, "/api/scenes/play/one-shot", HTTP_POST, Http::MAX_BODY_LARGE,
                     this, &SceneServer::handlePostPlayOneShotScene);
        server.on("/api/scenes/play", HTTP_POST, [this](AsyncWebServerRequest *r) {
            handlePostPlayLastScene(r);
        });
        server.on("/api/scenes/*", HTTP_GET, [this](AsyncWebServerRequest *r) {
            handleGetSceneById(r);
        });
        server.on("/api/scenes/*", HTTP_DELETE, [this](AsyncWebServerRequest *r) {
            handleDeleteScene(r);
        });
        server.on("/api/scenes/*", HTTP_POST, [this](AsyncWebServerRequest *r) {
            handlePostPlaySceneById(r);
        });
        server.on("/api/scenes", HTTP_GET, [this](AsyncWebServerRequest *r) {
            handleListScenes(r);
        });
        Http::onBody(server, "/api/scenes", HTTP_POST, Http::MAX_BODY_LARGE,
                     this, &SceneServer::handlePostSaveScene);
    }

    namespace {
        struct ListCtx {
            AsyncResponseStream *res;
            bool                 first;
        };

        void appendSceneMeta(const SceneMeta& meta, void *ctx)
        {
            auto *c = static_cast<ListCtx *>(ctx);
            char buf[192];

            snprintf(buf, sizeof(buf),
                     "%s{\"schemaVersion\":1,\"id\":\"%s\",\"name\":\"%s\",\"layersNum\":%u,\"duration\":%lu}",
                     c->first ? "" : ",", meta.id, meta.name, (unsigned)meta.layersNum,
                     (unsigned long)meta.duration);
            c->res->print(buf);
            c->first = false;
        }
    } // anonymous namespace

    void SceneServer::handleListScenes(AsyncWebServerRequest *req)
    {
        auto *repo = static_cast<LittleFsSceneRepository *>(&scenes);

        repo->lock().acquire();

        AsyncResponseStream *res = req->beginResponseStream("application/json");
        ListCtx ctx { res, true };

        res->print("[");
        repo->listMetasUnlocked(appendSceneMeta, &ctx);
        res->print("]");

        StoreLock *lockPtr = &repo->lock();

        req->onDisconnect([lockPtr]() {
            lockPtr->release();
        });

        req->send(res);
    }

    void SceneServer::handleGetSceneById(AsyncWebServerRequest *req)
    {
        char id[ENTRY_ID_MAX + 1];

        if (!Http::idFromUrl(req->url().c_str(), "/api/scenes/", id, sizeof(id)) ||
            !Http::isSafeId(id)) {
            Http::sendError(req, 400, "invalid_id");

            return;
        }

        if (scenes.isHiddenId(id) || !scenes.exists(id)) {
            Http::sendError(req, 404, "not_found");

            return;
        }

        auto *repo = static_cast<LittleFsSceneRepository *>(&scenes);

        repo->lock().acquire();

        IContentReader *reader = scenes.openContent(id);

        if (!reader) {
            repo->lock().release();
            Http::sendError(req, 404, "not_found");

            return;
        }

        sendLockedContent(req, repo->lock(), reader);
    }

    void SceneServer::handleDeleteScene(AsyncWebServerRequest *req)
    {
        char id[ENTRY_ID_MAX + 1];

        if (!Http::idFromUrl(req->url().c_str(), "/api/scenes/", id, sizeof(id)) ||
            !Http::isSafeId(id)) {
            Http::sendError(req, 400, "invalid_id");

            return;
        }

        if (!scenes.deleteEntry(id)) {
            Http::sendError(req, 404, "not_found");

            return;
        }

        Http::sendOk(req);
    }

    void SceneServer::handlePostSaveScene(AsyncWebServerRequest *req, const uint8_t *body, size_t len)
    {
        auto r = animService.saveScene((const char *)body, len);

        if (!r.ok()) {
            sendSceneError(req, r);

            return;
        }

        char resp[48];

        snprintf(resp, sizeof(resp), "{\"id\":\"%s\"}", r.savedId);
        Http::sendOkJson(req, resp);
    }

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
                                           x.self->appearance.paletteId(),
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

        appState.setLastPlayedSceneId(scenes.oneShotId(), false);
        deferPlay(req, parsed);
    }

    void SceneServer::handlePostPlayLastScene(AsyncWebServerRequest *req)
    {
        if (!appState.isOn()) {
            Http::sendError(req, 409, "system_off");

            return;
        }

        const char *lastId = appState.lastPlayedSceneId();

        if (lastId[0] == '\0') {
            Http::sendError(req, 404, "no_last_played_scene");

            return;
        }

        const char *id = appState.lastPlayedSceneIsStored() ? lastId : scenes.oneShotId();

        SceneParseResult *parsed = new (std::nothrow) SceneParseResult;

        if (!parsed) {
            Http::sendError(req, 500, "oom");

            return;
        }

        SceneResult r = animService.prepareById(id, *parsed);

        if (!r.ok()) {
            delete parsed;
            sendSceneError(req, r);

            return;
        }

        deferPlay(req, parsed);
    }

    void SceneServer::handlePostPlaySceneById(AsyncWebServerRequest *req)
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

        char id[ENTRY_ID_MAX + 1];

        if (!Http::idFromUrl(url, "/api/scenes/", id, sizeof(id)) || !Http::isSafeId(id)) {
            Http::sendError(req, 400, "invalid_id");

            return;
        }

        SceneParseResult *parsed = new (std::nothrow) SceneParseResult;

        if (!parsed) {
            Http::sendError(req, 500, "oom");

            return;
        }

        SceneResult r = animService.prepareById(id, *parsed);

        if (!r.ok()) {
            delete parsed;
            sendSceneError(req, r);

            return;
        }

        appState.setLastPlayedSceneId(id, true);
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
