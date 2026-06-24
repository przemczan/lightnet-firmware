#include "ConfigurationServer.hpp"
#include "HttpHelpers.hpp"
#include "HttpJsonCapacity.hpp"
#include "../../../Utils/SimpleJson.hpp"
#include <Arduino.h>
#include <string.h>
#include <stdio.h>

namespace Lightnet {
    ConfigurationServer::ConfigurationServer(
        AsyncWebServer&      _server,
        ConfigurationStore&  _config,
        TopologyConfigStore& _topology,
        ScenePlayer&         _player,
        MainLoopQueue&       _queue
    )
        : server(_server), config(_config), topology(_topology), player(_player), queue(_queue)
    {
    }

    void ConfigurationServer::begin()
    {
        registerRoutes();
    }

    void ConfigurationServer::registerRoutes()
    {
        Http::onRequest(server, "/api/configuration", HTTP_GET, this, &ConfigurationServer::handleGetConfiguration);
        Http::onBody(server, "/api/configuration", HTTP_PATCH, Http::MAX_BODY_LARGE,
                     this, &ConfigurationServer::handlePatchConfiguration);
    }

    void ConfigurationServer::handleGetConfiguration(AsyncWebServerRequest *req)
    {
        char buf[HttpJson::CONFIGURATION_GET_BUFFER];

        snprintf(buf, sizeof(buf), "{\"powerStateOnBoot\":%u,\"logicalRoot\":%u}",
                 (unsigned)config.powerStateOnBoot(),
                 (unsigned)topology.logicalRoot());

        Http::sendOkJson(req, buf);
    }

    void ConfigurationServer::handlePatchConfiguration(AsyncWebServerRequest *req, const uint8_t *body, size_t len)
    {
        const char *p   = (const char *)body;
        const char *end = p + len;

        if (!jsonEnterObject(p, end)) {
            Http::sendError(req, 400, "expected_object");

            return;
        }

        bool foundPower = false;
        bool foundRoot  = false;
        long root       = -1;
        char key[20];

        while (jsonNextKey(p, end, key, sizeof(key))) {
            if (strcmp(key, "powerStateOnBoot") == 0) {
                long v;

                if (!jsonReadUInt(p, end, &v)) {
                    Http::sendError(req, 400, "powerStateOnBoot: expected integer");

                    return;
                }

                if (!config.setPowerStateOnBoot((uint8_t)v)) {
                    Http::sendError(req, 422, "powerStateOnBoot: must be 0, 1, or 2");

                    return;
                }

                foundPower = true;
            } else if (strcmp(key, "logicalRoot") == 0) {
                if (!jsonReadUInt(p, end, &root)) {
                    Http::sendError(req, 400, "logicalRoot: not a uint");

                    return;
                }

                if (root < 0 || root > 255) {
                    Http::sendError(req, 422, "logicalRoot: must be 0-255");

                    return;
                }

                foundRoot = true;
            } else {
                jsonSkipValue(p, end);
            }
        }

        if (!foundPower && !foundRoot) {
            Http::sendError(req, 400, "no recognized fields");

            return;
        }

        if (foundRoot) {
            struct Args {
                ConfigurationServer *self;
                uint8_t              root;
            } args { this, (uint8_t)root };

            bool queued = queue.post(+[](const uint8_t *a, uint16_t) {
                Args x;

                memcpy(&x, a, sizeof(x));
                x.self->topology.setLogicalRoot(x.root);
                x.self->player.setLogicalRoot(x.root, millis());
            }, &args, sizeof(args));

            if (!queued) {
                Http::sendError(req, 503, "busy");

                return;
            }

            char buf[HttpJson::CONFIGURATION_PATCH_BUFFER];

            snprintf(buf, sizeof(buf), "{\"ok\":true,\"logicalRoot\":%u}", (unsigned)root);
            Http::sendAcceptedJson(req, buf);

            return;
        }

        Http::sendOk(req);
    }
}  // namespace Lightnet
