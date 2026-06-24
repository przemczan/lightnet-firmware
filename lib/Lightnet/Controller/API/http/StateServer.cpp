#include "StateServer.hpp"
#include "HttpHelpers.hpp"
#include "../../../Utils/SimpleJson.hpp"
#include "../../Actions/ControllerActions.hpp"
#include "../../Panels/PanelsInitializer.hpp"
#include "../../Panels/Panel.hpp"
#include "../websocket/PacketMirror.hpp"
#include <Arduino.h>
#include <string.h>

namespace Lightnet {
    StateServer::StateServer(
        AsyncWebServer&     _server,
        AppStateStore&      _appState,
        PanelsController&   _panelsController,
        ScenesService&      _animService,
        AnimationScheduler& _animScheduler,
        AppearanceService&  _appearance,
        MainLoopQueue&      _queue,
        PacketMirror *      _packetMirror
    )
        : server(_server), appState(_appState), panelsController(_panelsController),
        animService(_animService), animScheduler(_animScheduler), appearance(_appearance),
        queue(_queue), packetMirror(_packetMirror)
    {
    }

    void StateServer::begin()
    {
        registerRoutes();
    }

    void StateServer::registerRoutes()
    {
        Http::onRequest(server, "/api/state", HTTP_GET, this, &StateServer::handleGetState);
        Http::onBody(server, "/api/state/power", HTTP_POST, Http::MAX_BODY_SMALL,
                     this, &StateServer::handlePostPower);
    }

    void StateServer::handleGetState(AsyncWebServerRequest *req)
    {
        char buf[256];
        size_t pos = (size_t)snprintf(buf, sizeof(buf),
                                      "{\"isOn\":%s,\"lastPlayedSceneId\":",
                                      appState.isOn() ? "true" : "false");

        if (pos >= sizeof(buf)) {
            Http::sendError(req, 500, "response_overflow");

            return;
        }

        pos = jsonAppendQuotedString(buf, sizeof(buf), pos, appState.lastPlayedSceneId());

        if (pos == (size_t)-1 || pos + 96 >= sizeof(buf)) {
            Http::sendError(req, 500, "response_overflow");

            return;
        }

        int n = snprintf(buf + pos, sizeof(buf) - pos,
                         ",\"lastPlayedSceneIsStored\":%s,"
                         "\"playing\":%s,\"speed\":%.1f,\"controllerFirmware\":",
                         appState.lastPlayedSceneIsStored() ? "true" : "false",
                         animService.isPlaying() ? "true" : "false",
                         (double)animService.getSpeed());

        if (n <= 0) {
            Http::sendError(req, 500, "response_overflow");

            return;
        }

        pos += (size_t)n;
        pos = jsonAppendQuotedString(buf, sizeof(buf), pos, FW_VERSION);

        if (pos == (size_t)-1 || pos + 2 >= sizeof(buf)) {
            Http::sendError(req, 500, "response_overflow");

            return;
        }

        buf[pos++] = '}';
        buf[pos]   = '\0';
        Http::sendOkJson(req, buf);
    }

    void StateServer::handlePostPower(AsyncWebServerRequest *req, const uint8_t *body, size_t len)
    {
        const char *p   = (const char *)body;
        const char *end = p + len;

        if (!jsonEnterObject(p, end)) {
            Http::sendError(req, 400, "expected_object");

            return;
        }

        bool found    = false;
        bool newValue = appState.isOn();
        char key[8];

        while (jsonNextKey(p, end, key, sizeof(key))) {
            if (strcmp(key, "isOn") == 0) {
                if (!jsonReadBool(p, end, &newValue)) {
                    Http::sendError(req, 400, "isOn: expected bool");

                    return;
                }

                found = true;
            } else {
                jsonSkipValue(p, end);
            }
        }

        if (!found) {
            Http::sendError(req, 400, "isOn: required");

            return;
        }

        // Defer both the state flip and its packet-emitting effects to the main loop so
        // they stay atomic (a full queue leaves nothing changed). The response echoes the
        // requested value — it takes effect on the next tick.
        struct Args {
            StateServer *self;
            bool         on;
        } args { this, newValue };

        bool queued = queue.post(+[](const uint8_t *a, uint16_t) {
            Args x;

            memcpy(&x, a, sizeof(x));

            if (x.self->appState.setIsOn(x.on)) {
                ControllerPowerContext ctx{
                    x.self->appState,
                    x.self->panelsController,
                    x.self->animService,
                    x.self->animScheduler,
                    x.self->appearance,
                    LNPanelsInitializer,
                    x.self->packetMirror
                };
                ControllerActions::applyPowerEffects(ctx, x.on);
            }
        }, &args, sizeof(args));

        if (!queued) {
            Http::sendError(req, 503, "busy");

            return;
        }

        char buf[16];

        snprintf(buf, sizeof(buf), "{\"isOn\":%s}", newValue ? "true" : "false");
        Http::sendAcceptedJson(req, buf);
    }
}  // namespace Lightnet
