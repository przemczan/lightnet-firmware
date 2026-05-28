#include "SceneServer.hpp"
#include "../../../Utils/SimpleJson.hpp"
#include <Arduino.h>
#include <FS.h>
#ifdef ARDUINO_ARCH_ESP32
    #include <SPIFFS.h>
#endif
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

namespace Lightnet {
    namespace {
        bool isSafeName(const char *name)
        {
            if (!name || !name[0]) return false;

            for (const char *c = name; *c; c++) {
                if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') ||
                      (*c >= '0' && *c <= '9') || *c == '_' || *c == '-')) return false;
            }

            return strlen(name) <= 18;
        }

        bool nameFromUrl(const char *url, const char *prefix, char *out, size_t outLen)
        {
            size_t pfxLen = strlen(prefix);

            if (strncmp(url, prefix, pfxLen) != 0) return false;

            const char *start = url + pfxLen;
            const char *slash = strchr(start, '/');
            size_t len = slash ? (size_t)(slash - start) : strlen(start);

            if (len == 0 || len >= outLen) return false;

            memcpy(out, start, len);
            out[len] = '\0';

            return true;
        }

        struct BodyBuf {
            size_t  len, cap;
            uint8_t data[1];
        };

        bool appendBody(AsyncWebServerRequest *req, const uint8_t *data, size_t len, size_t total, size_t maxCap)
        {
            BodyBuf *buf = (BodyBuf *)req->_tempObject;

            if (!buf) {
                size_t cap = (total > 0) ? total : 256;

                if (cap > maxCap) cap = maxCap;

                buf = (BodyBuf *)malloc(sizeof(BodyBuf) + cap);

                if (!buf) return false;

                buf->len = 0;
                buf->cap = cap;
                req->_tempObject = buf;
                req->onDisconnect([req]() {
                    if (req->_tempObject) {
                        free(req->_tempObject);
                        req->_tempObject = nullptr;
                    }
                });
            }

            if (buf->len + len > buf->cap) return false;

            memcpy(buf->data + buf->len, data, len);
            buf->len += len;

            return true;
        }
    } // anonymous namespace

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

    void SceneServer::sendOk(AsyncWebServerRequest *req)
    {
        req->send(200, "application/json", "{\"ok\":true}");
    }

    void SceneServer::sendOkJson(AsyncWebServerRequest *req, const char *json)
    {
        req->send(200, "application/json", json);
    }

    void SceneServer::sendError(AsyncWebServerRequest *req, int code, const char *msg)
    {
        char buf[128];

        snprintf(buf, sizeof(buf), "{\"error\":\"%s\"}", msg ? msg : "error");
        req->send(code, "application/json", buf);
    }

    void SceneServer::sendSceneError(AsyncWebServerRequest *req, const SceneResult& r)
    {
        sendError(req, sceneErrorCode(r.err), r.msg);
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
        auto bodyLarge = [this](AsyncWebServerRequest *r, uint8_t *d, size_t l, size_t i, size_t t) {
                             if (!appendBody(r, d, l, t, MAX_BODY_LARGE)) sendError(r, 413, "body_too_large");
                         };

        auto dispatchLarge = [this](void (SceneServer::*m)(AsyncWebServerRequest *, const uint8_t *, size_t)) {
                                 return [this, m](AsyncWebServerRequest *r) {
                                            BodyBuf *buf = (BodyBuf *)r->_tempObject;

                                            if (!buf) {
                                                sendError(r, 400, "empty_body");

                                                return;
                                            }

                                            (this->*m)(r, buf->data, buf->len);
                                 };
                             };

        server.on("/api/scenes", HTTP_GET, [this](AsyncWebServerRequest *r){
            handleListScenes(r);
        });
        server.on("/api/scenes/status", HTTP_GET, [this](AsyncWebServerRequest *r){
            handleGetSceneStatus(r);
        });
        server.on("/api/scenes/stop", HTTP_POST, [this](AsyncWebServerRequest *r){
            handlePostStopScene(r);
        });
        server.on("/api/scenes/speed", HTTP_POST, dispatchLarge(&SceneServer::handlePostSetSpeed), nullptr, bodyLarge);
        server.on("/api/scenes/play", HTTP_POST, dispatchLarge(&SceneServer::handlePostPlayScene), nullptr, bodyLarge);
        server.on("/api/scenes", HTTP_POST, dispatchLarge(&SceneServer::handlePostSaveScene), nullptr, bodyLarge);
        server.on("/api/scenes/*", HTTP_GET, [this](AsyncWebServerRequest *r){
            handleGetSceneByName(r);
        });
        server.on("/api/scenes/*", HTTP_DELETE, [this](AsyncWebServerRequest *r){
            handleDeleteScene(r);
        });
        server.on("/api/scenes/*", HTTP_POST, [this](AsyncWebServerRequest *r){
            handlePostPlaySceneByName(r);
        });
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
        sendOkJson(req, buf);
    }

    void SceneServer::handleGetSceneStatus(AsyncWebServerRequest *req)
    {
        char buf[128];

        player.writeStatusJson(buf, sizeof(buf));
        sendOkJson(req, buf);
    }

    void SceneServer::handleGetSceneByName(AsyncWebServerRequest *req)
    {
        char name[24];

        if (!nameFromUrl(req->url().c_str(), "/api/scenes/", name, sizeof(name)) || !isSafeName(name)) {
            sendError(req, 400, "invalid_name");

            return;
        }

        char path[36];

        snprintf(path, sizeof(path), "/scenes/%s.json", name);

        if (!SPIFFS.exists(path)) {
            sendError(req, 404, "not_found");

            return;
        }

        req->send(SPIFFS, path, "application/json");
    }

    void SceneServer::handleDeleteScene(AsyncWebServerRequest *req)
    {
        char name[24];

        if (!nameFromUrl(req->url().c_str(), "/api/scenes/", name, sizeof(name)) || !isSafeName(name)) {
            sendError(req, 400, "invalid_name");

            return;
        }

        char path[36];

        snprintf(path, sizeof(path), "/scenes/%s.json", name);

        if (!SPIFFS.exists(path)) {
            sendError(req, 404, "not_found");

            return;
        }

        SPIFFS.remove(path);
        sendOk(req);
    }

    void SceneServer::handlePostSaveScene(AsyncWebServerRequest *req, const uint8_t *body, size_t len)
    {
        auto r = animService.saveScene((const char *)body, len);

        if (!r.ok()) {
            sendSceneError(req, r);

            return;
        }

        sendOk(req);
    }

    void SceneServer::handlePostPlayScene(AsyncWebServerRequest *req, const uint8_t *body, size_t len)
    {
        auto r = animService.playSceneInline((const char *)body, len);

        if (!r.ok()) {
            sendSceneError(req, r);

            return;
        }

        sendOk(req);
    }

    void SceneServer::handlePostPlaySceneByName(AsyncWebServerRequest *req)
    {
        const char *url = req->url().c_str();

        if (!strstr(url + strlen("/api/scenes/"), "/play")) {
            sendError(req, 404, "not_found");

            return;
        }

        char name[24];

        if (!nameFromUrl(url, "/api/scenes/", name, sizeof(name)) || !isSafeName(name)) {
            sendError(req, 400, "invalid_name");

            return;
        }

        auto r = animService.playSceneByName(name);

        if (!r.ok()) {
            sendSceneError(req, r);

            return;
        }

        sendOk(req);
    }

    void SceneServer::handlePostStopScene(AsyncWebServerRequest *req)
    {
        animService.stopScene();
        sendOk(req);
    }

    void SceneServer::handlePostSetSpeed(AsyncWebServerRequest *req, const uint8_t *body, size_t len)
    {
        // Body: {"speed": <float 0.1-10.0>}
        const char *p   = (const char *)body;
        const char *end = p + len;

        if (!jsonEnterObject(p, end)) { sendError(req, 400, "expected_object"); return; }

        float speed = 1.0f;
        bool found = false;
        char key[12];

        while (jsonNextKey(p, end, key, sizeof(key))) {
            if (strcmp(key, "speed") == 0) {
                if (!jsonReadFloat(p, end, &speed)) { sendError(req, 400, "speed: not a number"); return; }

                found = true;
            } else {
                jsonSkipValue(p, end);
            }
        }

        if (!found) { sendError(req, 400, "speed: required"); return; }

        if (speed < 0.1f || speed > 10.0f) { sendError(req, 422, "speed: must be 0.1-10.0"); return; }

        animService.setSceneSpeed(speed);

        char buf[48];
        snprintf(buf, sizeof(buf), "{\"ok\":true,\"speed\":%.1f}", (double)speed);
        sendOkJson(req, buf);
    }
}  // namespace Lightnet
