#include "ConfigurationServer.hpp"
#include "HttpHelpers.hpp"
#include "../../../Utils/SimpleJson.hpp"
#include <Arduino.h>
#include <string.h>

namespace Lightnet {
    ConfigurationServer::ConfigurationServer(AsyncWebServer& _server, ConfigurationStore& _config)
        : server(_server), config(_config)
    {
    }

    void ConfigurationServer::begin()
    {
        registerRoutes();
    }

    void ConfigurationServer::registerRoutes()
    {
        server.on("/api/configuration", HTTP_GET, [this](AsyncWebServerRequest *r) {
            handleGetConfiguration(r);
        });
        Http::onBody(server, "/api/configuration", HTTP_PATCH, Http::MAX_BODY_SMALL,
                     this, &ConfigurationServer::handlePatchConfiguration);
    }

    void ConfigurationServer::handleGetConfiguration(AsyncWebServerRequest *req)
    {
        char buf[32];

        snprintf(buf, sizeof(buf), "{\"powerStateOnBoot\":%u}", (unsigned)config.powerStateOnBoot());
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

        bool found = false;
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

                found = true;
            } else {
                jsonSkipValue(p, end);
            }
        }

        if (!found) {
            Http::sendError(req, 400, "powerStateOnBoot: required");

            return;
        }

        Http::sendOk(req);
    }
}  // namespace Lightnet
