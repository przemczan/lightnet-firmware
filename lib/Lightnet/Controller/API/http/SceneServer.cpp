#include "SceneServer.hpp"
#include "HttpHelpers.hpp"
#include "HttpJsonCapacity.hpp"
#include "../../../Utils/EntryId.hpp"
#include "../../../Core/Controller/SceneWriter.hpp"
#include "../../../Utils/SimpleJson.hpp"
#include <Arduino.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

namespace Lightnet {
    SceneServer::SceneServer(
        AsyncWebServer&    _server,
        SceneStore&        _scenes,
        ScenePlayer&       _player,
        ScenesService&     _animService,
        AppStateStore&     _appState,
        AppearanceService& _appearance,
        MainLoopQueue&     _queue
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
        Http::onRequest(server, "/api/scenes/stop", HTTP_POST, this, &SceneServer::handlePostStopScene);
        Http::onBody(server, "/api/scenes/speed", HTTP_POST, Http::MAX_BODY_LARGE,
                     this, &SceneServer::handlePostSetSpeed);
        Http::onBody(server, "/api/scenes/play/one-shot", HTTP_POST, Http::MAX_BODY_LARGE,
                     this, &SceneServer::handlePostPlayOneShotScene);
        Http::onRequest(server, "/api/scenes/play", HTTP_POST, this, &SceneServer::handlePostPlayLastScene);
        Http::onRequest(server, "/api/scenes/*", HTTP_GET, this, &SceneServer::handleGetSceneById);
        Http::onRequest(server, "/api/scenes/*", HTTP_DELETE, this, &SceneServer::handleDeleteScene);
        Http::onRequest(server, "/api/scenes/*", HTTP_POST, this, &SceneServer::handlePostPlaySceneById);
        Http::onBody(server, "/api/scenes/*", HTTP_PATCH, Http::MAX_BODY_LARGE,
                     this, &SceneServer::handlePatchUpdateScene);
        Http::onRequest(server, "/api/scenes", HTTP_GET, this, &SceneServer::handleListScenes);
        Http::onBody(server, "/api/scenes", HTTP_POST, Http::MAX_BODY_LARGE,
                     this, &SceneServer::handlePostCreateScene);
    }

    namespace {
        struct ListScenesContext {
            char * buf;
            size_t cap;
            size_t pos;
            bool   first;
            bool   ok;
        };

        bool appendListSceneMeta(ListScenesContext *ctx, const SceneMeta& meta)
        {
            if (!ctx->ok) return false;

            if (!ctx->first) {
                if (ctx->pos + 1 >= ctx->cap) {
                    ctx->ok = false;

                    return false;
                }

                ctx->buf[ctx->pos++] = ',';
            }

            size_t pos = ctx->pos;

            pos += (size_t)snprintf(ctx->buf + pos, ctx->cap - pos,
                                    "{\"schemaVersion\":1,\"id\":");

            if (pos >= ctx->cap) {
                ctx->ok = false;

                return false;
            }

            pos = jsonAppendQuotedString(ctx->buf, ctx->cap, pos, meta.id);

            static const char NAME_FIELD[] = ",\"name\":";
            constexpr size_t nameFieldLen = sizeof(NAME_FIELD) - 1;

            if (pos == (size_t)-1 || pos + nameFieldLen >= ctx->cap) {
                ctx->ok = false;

                return false;
            }

            memcpy(ctx->buf + pos, NAME_FIELD, nameFieldLen);
            pos += nameFieldLen;

            pos = jsonAppendQuotedString(ctx->buf, ctx->cap, pos, meta.name);

            if (pos == (size_t)-1) {
                ctx->ok = false;

                return false;
            }

            int n = snprintf(ctx->buf + pos, ctx->cap - pos,
                             ",\"layerCount\":%u,\"duration\":%lu}",
                             (unsigned)meta.layerCount, (unsigned long)meta.duration);

            if (n <= 0 || pos + (size_t)n >= ctx->cap) {
                ctx->ok = false;

                return false;
            }

            ctx->pos    = pos + (size_t)n;
            ctx->first  = false;

            return true;
        }

        void foreachListSceneMeta(const SceneMeta& meta, void *userContext)
        {
            appendListSceneMeta(static_cast<ListScenesContext *>(userContext), meta);
        }
    } // anonymous namespace

    void SceneServer::handleListScenes(AsyncWebServerRequest *req)
    {
        size_t cap = HttpJson::sceneListCapacity(scenes.count());

        char *buf = (char *)malloc(cap);

        if (!buf) {
            Http::sendError(req, 500, "oom");

            return;
        }

        ListScenesContext ctx = { buf, cap, 0, true, true };

        buf[ctx.pos++] = '[';

        SceneStoreResult result = scenes.foreachMeta(foreachListSceneMeta, &ctx);

        if (result != SCENE_STORE_OK || !ctx.ok || ctx.pos + HttpJson::CLOSE_RESERVE > ctx.cap) {
            free(buf);
            Http::sendError(req, 500, result != SCENE_STORE_OK ? "store_error" : "response_overflow");

            return;
        }

        buf[ctx.pos++] = ']';
        buf[ctx.pos]   = '\0';

        Http::sendOkJson(req, buf);
        free(buf);
    }

    void SceneServer::handleGetSceneById(AsyncWebServerRequest *req)
    {
        char id[sizeof(SceneMeta::id)];

        if (!Http::idFromUrl(req->url().c_str(), "/api/scenes/", id, sizeof(id)) ||
            !Http::isSafeId(id)) {
            Http::sendError(req, 400, "invalid_id");

            return;
        }

        if (scenes.isHiddenId(id) || !scenes.exists(id)) {
            Http::sendError(req, 404, "not_found");

            return;
        }

        SceneRecord record = {};

        if (scenes.get(id, record) != SCENE_STORE_OK) {
            Http::sendError(req, 404, "not_found");

            return;
        }

        char *json = (char *)malloc(SceneStore::MAX_SCENE_BYTES + 1);

        if (!json) {
            Http::sendError(req, 500, "oom");

            return;
        }

        int n = serializeScene(record, json, SceneStore::MAX_SCENE_BYTES + 1);

        if (n < 0) {
            free(json);
            Http::sendError(req, 500, "serialize_failed");

            return;
        }

        Http::sendOkJson(req, json);
        free(json);
    }

    void SceneServer::handleDeleteScene(AsyncWebServerRequest *req)
    {
        char id[sizeof(SceneMeta::id)];

        if (!Http::idFromUrl(req->url().c_str(), "/api/scenes/", id, sizeof(id)) ||
            !Http::isSafeId(id)) {
            Http::sendError(req, 400, "invalid_id");

            return;
        }

        if (scenes.remove(id) != SCENE_STORE_OK) {
            Http::sendError(req, 404, "not_found");

            return;
        }

        Http::sendOk(req);
    }

    void SceneServer::handlePostCreateScene(AsyncWebServerRequest *req, const uint8_t *body, size_t len)
    {
        auto r = animService.createScene((const char *)body, len);

        if (!r.ok()) {
            sendSceneError(req, r);

            return;
        }

        char resp[HttpJson::SCENE_ID_BUFFER];

        if (jsonWriteObjectStringField(resp, sizeof(resp), "id", r.savedId) < 0) {
            Http::sendError(req, 500, "response_overflow");

            return;
        }

        Http::sendOkJson(req, resp);
    }

    void SceneServer::handlePatchUpdateScene(AsyncWebServerRequest *req, const uint8_t *body, size_t len)
    {
        char id[sizeof(SceneMeta::id)];

        if (!Http::idFromUrl(req->url().c_str(), "/api/scenes/", id, sizeof(id)) ||
            !Http::isSafeId(id)) {
            Http::sendError(req, 400, "invalid_id");

            return;
        }

        auto r = animService.updateScene(id, (const char *)body, len);

        if (!r.ok()) {
            sendSceneError(req, r);

            return;
        }

        char resp[HttpJson::SCENE_ID_BUFFER];

        if (jsonWriteObjectStringField(resp, sizeof(resp), "id", r.savedId) < 0) {
            Http::sendError(req, 500, "response_overflow");

            return;
        }

        Http::sendOkJson(req, resp);
    }

    void SceneServer::deferPlayById(AsyncWebServerRequest *req, const char *id)
    {
        struct Args {
            SceneServer *self;
            char         id[sizeof(SceneMeta::id)];
        } args { this, {} };

        strncpy(args.id, id, sizeof(args.id) - 1);

        bool queued = queue.post(+[](const uint8_t *a, uint16_t) {
            Args x;

            memcpy(&x, a, sizeof(x));
            x.self->animService.playSceneById(x.id,
                                              x.self->appearance.paletteName(),
                                              x.self->appearance.baseColors());
        }, &args, sizeof(args));

        if (!queued) {
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

        SceneRecord parsed = {};

        SceneResult r = animService.prepareInline((const char *)body, len, parsed);

        if (!r.ok()) {
            sendSceneError(req, r);

            return;
        }

        appState.setLastPlayedSceneId(scenes.oneShotId(), false);
        deferPlayById(req, scenes.oneShotId());
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

        SceneRecord parsed = {};
        SceneResult r = animService.prepareById(id, parsed);

        if (!r.ok()) {
            sendSceneError(req, r);

            return;
        }

        deferPlayById(req, id);
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

        char id[sizeof(SceneMeta::id)];

        if (!Http::idFromUrl(url, "/api/scenes/", id, sizeof(id)) || !Http::isSafeId(id)) {
            Http::sendError(req, 400, "invalid_id");

            return;
        }

        SceneRecord parsed = {};
        SceneResult r = animService.prepareById(id, parsed);

        if (!r.ok()) {
            sendSceneError(req, r);

            return;
        }

        appState.setLastPlayedSceneId(id, true);
        deferPlayById(req, id);
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

        char buf[HttpJson::SCENE_SPEED_BUFFER];

        snprintf(buf, sizeof(buf), "{\"ok\":true,\"speed\":%.1f}", (double)speed);
        Http::sendAcceptedJson(req, buf);
    }
}  // namespace Lightnet
