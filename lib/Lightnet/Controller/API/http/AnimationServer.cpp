#include "AnimationServer.hpp"
#include "../../../Utils/SimpleJson.hpp"
#include <Arduino.h>
#include <string.h>
#include <stdlib.h>

namespace Lightnet {
    namespace {
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

    AnimationServer::AnimationServer(
        AsyncWebServer&     _server,
        AnimationService&   _animService,
        AnimationScheduler& _scheduler,
        AppearanceStore&    _appearance
    )
        : server(_server), animService(_animService), scheduler(_scheduler), appearance(_appearance)
    {
    }

    void AnimationServer::begin()
    {
        registerRoutes();
    }

    void AnimationServer::sendOk(AsyncWebServerRequest *req)
    {
        req->send(200, "application/json", "{\"ok\":true}");
    }

    void AnimationServer::sendOkJson(AsyncWebServerRequest *req, const char *json)
    {
        req->send(200, "application/json", json);
    }

    void AnimationServer::sendError(AsyncWebServerRequest *req, int code, const char *msg)
    {
        char buf[128];

        snprintf(buf, sizeof(buf), "{\"error\":\"%s\"}", msg ? msg : "error");
        req->send(code, "application/json", buf);
    }

    void AnimationServer::sendSceneError(AsyncWebServerRequest *req, const SceneResult& r)
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

    void AnimationServer::registerRoutes()
    {
        auto bodyLarge = [this](AsyncWebServerRequest *r, uint8_t *d, size_t l, size_t i, size_t t) {
                             if (!appendBody(r, d, l, t, MAX_BODY_LARGE)) sendError(r, 413, "body_too_large");
                         };
        auto bodySmall = [this](AsyncWebServerRequest *r, uint8_t *d, size_t l, size_t i, size_t t) {
                             if (!appendBody(r, d, l, t, MAX_BODY_SMALL)) sendError(r, 413, "body_too_large");
                         };

        auto dispatchLarge = [this](void (AnimationServer::*m)(AsyncWebServerRequest *, const uint8_t *, size_t)) {
                                 return [this, m](AsyncWebServerRequest *r) {
                                            BodyBuf *buf = (BodyBuf *)r->_tempObject;

                                            if (!buf) {
                                                sendError(r, 400, "empty_body");

                                                return;
                                            }

                                            (this->*m)(r, buf->data, buf->len);
                                 };
                             };
        auto dispatchSmall = dispatchLarge;

        server.on("/api/animations/play", HTTP_POST, dispatchLarge(&AnimationServer::handleOneShotPlay), nullptr, bodyLarge);
        server.on("/api/animations/trigger", HTTP_POST, dispatchSmall(&AnimationServer::handleAnimTrigger), nullptr, bodySmall);
    }

    void AnimationServer::handleOneShotPlay(AsyncWebServerRequest *req, const uint8_t *body, size_t len)
    {
        auto r = animService.playOneShot((const char *)body, len,
                                         appearance.paletteName(), appearance.baseColors());

        if (!r.ok()) {
            sendSceneError(req, r);

            return;
        }

        sendOk(req);
    }

    void AnimationServer::handleAnimTrigger(AsyncWebServerRequest *req, const uint8_t *body, size_t len)
    {
        SimpleJson j(body, len);
        long grp = j.getInt("group");
        long val = j.getInt("value");

        if (grp <= 0 || grp > 254) {
            sendError(req, 422, "group_out_of_range");

            return;
        }

        if (val < 0) val = 200;

        if (val > 255) {
            sendError(req, 422, "value_out_of_range");

            return;
        }

        scheduler.triggerGroup((uint8_t)grp, (uint8_t)val);
        sendOk(req);
    }
}  // namespace Lightnet
