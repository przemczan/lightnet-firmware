#include "AnimationServer.hpp"
#include "HttpHelpers.hpp"
#include "../../../Utils/SimpleJson.hpp"
#include "../../../Core/Controller/Scene/ScenePlayer.hpp"  // SceneLayer
#include <Arduino.h>
#include <string.h>
#include <new>

namespace Lightnet {
    AnimationServer::AnimationServer(
        AsyncWebServer&     _server,
        AnimationService&   _animService,
        AnimationScheduler& _scheduler,
        AppearanceStore&    _appearance,
        AppStateStore&      _appState,
        MainLoopQueue&      _queue
    )
        : server(_server), animService(_animService), scheduler(_scheduler),
        appearance(_appearance), appState(_appState), queue(_queue)
    {
    }

    void AnimationServer::begin()
    {
        registerRoutes();
    }

    void AnimationServer::sendSceneError(AsyncWebServerRequest *req, const SceneResult& r)
    {
        Http::sendError(req, sceneErrorCode(r.err), r.msg);
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
        Http::onBody(server, "/api/animations/play", HTTP_POST, Http::MAX_BODY_LARGE,
                     this, &AnimationServer::handleOneShotPlay);
        Http::onBody(server, "/api/animations/trigger", HTTP_POST, Http::MAX_BODY_SMALL,
                     this, &AnimationServer::handleAnimTrigger);
    }

    void AnimationServer::handleOneShotPlay(AsyncWebServerRequest *req, const uint8_t *body, size_t len)
    {
        if (!appState.isOn()) {
            Http::sendError(req, 409, "system_off");

            return;
        }

        // Heap-own the parsed layer (~280 B); the deferred task plays it then frees it.
        SceneLayer *layer = new (std::nothrow) SceneLayer;

        if (!layer) {
            Http::sendError(req, 500, "oom");

            return;
        }

        SceneResult r = animService.prepareOneShot((const char *)body, len, *layer);

        if (!r.ok()) {
            delete layer;
            sendSceneError(req, r);

            return;
        }

        struct Args {
            AnimationServer *self;
            SceneLayer *     layer;
        } args { this, layer };

        bool queued = queue.post(+[](const uint8_t *a, uint16_t) {
            Args x;

            memcpy(&x, a, sizeof(x));
            x.self->animService.playParsedOneShot(*x.layer,
                                                  x.self->appearance.paletteName(),
                                                  x.self->appearance.baseColors());
            delete x.layer;
        }, &args, sizeof(args));

        if (!queued) {
            delete layer;
            Http::sendError(req, 503, "busy");

            return;
        }

        Http::sendAccepted(req);
    }

    void AnimationServer::handleAnimTrigger(AsyncWebServerRequest *req, const uint8_t *body, size_t len)
    {
        if (!appState.isOn()) {
            Http::sendError(req, 409, "system_off");

            return;
        }

        SimpleJson j(body, len);

        // "group" accepts a numeric ID or a string layer name.
        uint8_t grp = 0;
        const char *rawGrp = j.rawValue("group");

        if (!rawGrp) {
            Http::sendError(req, 422, "group_missing");

            return;
        }

        if (*rawGrp == '"') {
            char groupStr[17];

            if (!j.getString("group", groupStr, sizeof(groupStr)) || !groupStr[0]) {
                Http::sendError(req, 422, "group_invalid");

                return;
            }

            grp = animService.groupIdForName(groupStr);

            if (grp == 0) {
                Http::sendError(req, 422, "group_not_found");

                return;
            }
        } else {
            long v = j.getInt("group");

            if (v <= 0 || v > 254) {
                Http::sendError(req, 422, "group_out_of_range");

                return;
            }

            grp = (uint8_t)v;
        }

        long val = j.getInt("value");

        if (val < 0) val = 200;

        if (val > 255) {
            Http::sendError(req, 422, "value_out_of_range");

            return;
        }

        struct Args {
            AnimationServer *self;
            uint8_t          grp;
            uint8_t          value;
        } args { this, grp, (uint8_t)val };

        bool queued = queue.post(+[](const uint8_t *a, uint16_t) {
            Args x;

            memcpy(&x, a, sizeof(x));
            x.self->scheduler.triggerGroup(x.grp, x.value);
        }, &args, sizeof(args));

        if (!queued) {
            Http::sendError(req, 503, "busy");

            return;
        }

        Http::sendAccepted(req);
    }
}  // namespace Lightnet
