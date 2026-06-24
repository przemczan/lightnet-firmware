#include "SceneServer.hpp"
#include "HttpHelpers.hpp"
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
        // Heap state for the chunked /api/scenes listing. Owned solely via
        // req->_tempObject + req->onDisconnect (see handleListScenes) — freed
        // exactly once, never by the fill callback itself.
        struct ListScenesState : Http::detail::RequestContext {
            SceneStore *scenes;
            size_t      cursor;         // next DB slot offset to scan from
            bool        emittedOpenBracket;
            bool        first;          // controls comma placement
            bool        dbExhausted;
            bool        emittedCloseBracket;
            char        pending[224];   // one JSON piece: bracket, or one meta entry + separator
            size_t      pendingLen;
            size_t      pendingPos;     // read cursor into pending
        };

        // Serializes one meta entry (with leading comma if needed) into
        // state->pending, resetting the pending read cursor to the start.
        void appendPendingSceneEntry(ListScenesState *state, const SceneMeta& meta)
        {
            size_t pos = 0;

            if (!state->first) {
                state->pending[0] = ',';
                pos = 1;
            }

            pos += (size_t)snprintf(state->pending + pos, sizeof(state->pending) - pos,
                                    "{\"schemaVersion\":1,\"id\":");

            if (pos >= sizeof(state->pending)) {
                state->pendingLen = 0;
                state->pendingPos = 0;

                return;
            }

            pos = jsonAppendQuotedString(state->pending, sizeof(state->pending), pos, meta.id);

            static const char NAME_FIELD[] = ",\"name\":";
            constexpr size_t nameFieldLen = sizeof(NAME_FIELD) - 1;

            if (pos == (size_t)-1 || pos + nameFieldLen >= sizeof(state->pending)) {
                state->pendingLen = 0;
                state->pendingPos = 0;

                return;
            }

            memcpy(state->pending + pos, NAME_FIELD, nameFieldLen);
            pos += nameFieldLen;

            pos = jsonAppendQuotedString(state->pending, sizeof(state->pending), pos, meta.name);

            if (pos == (size_t)-1) {
                state->pendingLen = 0;
                state->pendingPos = 0;

                return;
            }

            int n = snprintf(state->pending + pos, sizeof(state->pending) - pos,
                             ",\"layerCount\":%u,\"duration\":%lu}",
                             (unsigned)meta.layerCount, (unsigned long)meta.duration);

            state->pendingLen = pos + ((n > 0) ? (size_t)n : 0);
            state->pendingPos = 0;
            state->first      = false;
        }

        size_t sceneListFill(ListScenesState *state, uint8_t *buf, size_t maxLen)
        {
            size_t written = 0;

            while (written < maxLen) {
                if (state->pendingPos < state->pendingLen) {
                    size_t n = state->pendingLen - state->pendingPos;

                    if (n > maxLen - written) n = maxLen - written;

                    memcpy(buf + written, state->pending + state->pendingPos, n);
                    state->pendingPos += n;
                    written           += n;

                    continue;
                }

                if (!state->emittedOpenBracket) {
                    state->pending[0]  = '[';
                    state->pendingLen  = 1;
                    state->pendingPos  = 0;
                    state->emittedOpenBracket = true;

                    continue;
                }

                if (!state->dbExhausted) {
                    SceneMeta meta       = {};
                    size_t nextCursor = state->cursor;
                    bool found      = false;

                    SceneStoreResult result =
                        state->scenes->nextMeta(state->cursor, meta, nextCursor, found);

                    state->cursor = nextCursor;

                    if (result != SCENE_STORE_OK || !found) {
                        state->dbExhausted = true;

                        continue;
                    }

                    appendPendingSceneEntry(state, meta);

                    continue;
                }

                if (!state->emittedCloseBracket) {
                    state->pending[0]  = ']';
                    state->pendingLen  = 1;
                    state->pendingPos  = 0;
                    state->emittedCloseBracket = true;

                    continue;
                }

                break;
            }

            return written;
        }
    } // anonymous namespace

    void SceneServer::handleListScenes(AsyncWebServerRequest *req)
    {
        auto *state = (ListScenesState *)malloc(sizeof(ListScenesState));

        if (!state) {
            Http::sendError(req, 500, "oom");

            return;
        }

        if (req->_tempObject) {
            state->startMs = static_cast<Http::detail::RequestContext *>(req->_tempObject)->startMs;
            free(req->_tempObject);
        } else {
            state->startMs = millis();
        }

        state->scenes              = &scenes;
        state->cursor              = RECORDS_START_OFFSET;
        state->emittedOpenBracket  = false;
        state->first               = true;
        state->dbExhausted         = false;
        state->emittedCloseBracket = false;
        state->pendingLen          = 0;
        state->pendingPos          = 0;

        req->_tempObject = state;
        Http::onDisconnectLogged(req, [req]() {
            if (req->_tempObject) {
                free(req->_tempObject);
                req->_tempObject = nullptr;
            }
        });

        AsyncWebServerResponse *res = req->beginChunkedResponse(
            "application/json",
            [state, req](uint8_t *buf, size_t maxLen, size_t /*index*/) -> size_t {
            size_t written = sceneListFill(state, buf, maxLen);

            Http::logFillTick(req, written, maxLen);

            if (written == 0) Http::logChunkedComplete(req);

            return written;
        });

        Http::sendOkChunked(req, res);
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

        char resp[48];

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

        char resp[48];

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

        char buf[48];

        snprintf(buf, sizeof(buf), "{\"ok\":true,\"speed\":%.1f}", (double)speed);
        Http::sendAcceptedJson(req, buf);
    }
}  // namespace Lightnet
