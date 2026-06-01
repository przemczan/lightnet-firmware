#include "StateServer.hpp"
#include "HttpHelpers.hpp"
#include "../../../Utils/SimpleJson.hpp"
#include "../../Panels/PanelsInitializer.hpp"
#include "../../Panels/Panel.hpp"
#include <Arduino.h>
#include <string.h>

namespace Lightnet {
    StateServer::StateServer(
        AsyncWebServer&     _server,
        AppStateStore&      _appState,
        PanelsController&   _panelsController,
        AnimationService&   _animService,
        AnimationScheduler& _animScheduler,
        AppearanceStore&    _appearance
    )
        : server(_server), appState(_appState), panelsController(_panelsController),
        animService(_animService), animScheduler(_animScheduler), appearance(_appearance)
    {
    }

    void StateServer::begin()
    {
        registerRoutes();
    }

    void StateServer::registerRoutes()
    {
        server.on("/api/state/power", HTTP_GET, [this](AsyncWebServerRequest *r) {
            handleGetPower(r);
        });
        Http::onBody(server, "/api/state/power", HTTP_POST, Http::MAX_BODY_SMALL,
                     this, &StateServer::handlePostPower);
    }

    void StateServer::handleGetPower(AsyncWebServerRequest *req)
    {
        char buf[16];

        snprintf(buf, sizeof(buf), "{\"isOn\":%s}", appState.isOn() ? "true" : "false");
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

        applyIsOn(newValue);

        char buf[16];

        snprintf(buf, sizeof(buf), "{\"isOn\":%s}", appState.isOn() ? "true" : "false");
        Http::sendOkJson(req, buf);
    }

    void StateServer::applyIsOn(bool newValue)
    {
        if (!appState.setIsOn(newValue)) return; // no change

        List<Panel *> *panels = LNPanelsInitializer.getPanels();

        if (!newValue) {
            animService.stopScene();
            animScheduler.clearAllPanelQueues();

            for (uint16_t i = 0; i < panels->getSize(); i++) {
                panelsController.turnOff(panels->get(i)->index);
            }
        } else {
            for (uint16_t i = 0; i < panels->getSize(); i++) {
                panelsController.turnOn(panels->get(i)->index);
            }

            appearance.reapply();
        }
    }
}  // namespace Lightnet
