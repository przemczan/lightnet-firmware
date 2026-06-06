#include "TopologyServer.hpp"
#include "HttpHelpers.hpp"
#include "../../../Utils/SimpleJson.hpp"
#include <Arduino.h>
#include <string.h>
#include <stdio.h>

namespace Lightnet {
    TopologyServer::TopologyServer(AsyncWebServer& _server, TopologyConfigStore& _store, ScenePlayer& _player)
        : server(_server), store(_store), player(_player)
    {
    }

    void TopologyServer::begin()
    {
        registerRoutes();
    }

    void TopologyServer::registerRoutes()
    {
        // Specific route before the general /api/topology GET.
        Http::onBody(server, "/api/topology/root", HTTP_PUT, Http::MAX_BODY_SMALL,
                     this, &TopologyServer::handlePutRoot);

        server.on("/api/topology", HTTP_GET, [this](AsyncWebServerRequest *r) {
            handleGetTopology(r);
        });

        server.on("/api/panel-tags", HTTP_GET, [this](AsyncWebServerRequest *r) {
            handleGetTags(r);
        });
        Http::onBody(server, "/api/panel-tags", HTTP_PUT, Http::MAX_BODY_LARGE,
                     this, &TopologyServer::handlePutTags);
    }

    void TopologyServer::handleGetTopology(AsyncWebServerRequest *req)
    {
        // Heap, not stack: a full tag map is ~1.5 KB and async callbacks run on a tight stack.
        char *buf = (char *)malloc(2048);

        if (!buf) {
            Http::sendError(req, 500, "oom");

            return;
        }

        store.writeJson(buf, 2048);
        Http::sendOkJson(req, buf);
        free(buf);
    }

    void TopologyServer::handleGetTags(AsyncWebServerRequest *req)
    {
        char *buf = (char *)malloc(2048);

        if (!buf) {
            Http::sendError(req, 500, "oom");

            return;
        }

        store.writeTagsJson(buf, 2048);
        Http::sendOkJson(req, buf);
        free(buf);
    }

    void TopologyServer::handlePutRoot(AsyncWebServerRequest *req, const uint8_t *body, size_t len)
    {
        // Body: {"logicalRoot": <1-255, or 0 to reset to the physical root>}
        const char *p   = (const char *)body;
        const char *end = p + len;

        if (!jsonEnterObject(p, end)) {
            Http::sendError(req, 400, "expected_object");

            return;
        }

        long root  = -1;
        char key[16];

        while (jsonNextKey(p, end, key, sizeof(key))) {
            if (strcmp(key, "logicalRoot") == 0) {
                if (!jsonReadUInt(p, end, &root)) {
                    Http::sendError(req, 400, "logicalRoot: not a uint");

                    return;
                }
            } else {
                jsonSkipValue(p, end);
            }
        }

        if (root < 0 || root > 255) {
            Http::sendError(req, 422, "logicalRoot: must be 0-255");

            return;
        }

        store.setLogicalRoot((uint8_t)root);
        player.setLogicalRoot((uint8_t)root, millis()); // apply to a playing scene immediately

        char buf[48];

        snprintf(buf, sizeof(buf), "{\"ok\":true,\"logicalRoot\":%u}", (unsigned)store.logicalRoot());
        Http::sendOkJson(req, buf);
    }

    void TopologyServer::handlePutTags(AsyncWebServerRequest *req, const uint8_t *body, size_t len)
    {
        char err[64] = { 0 };

        if (!store.replaceTags((const char *)body, len, err, sizeof(err))) {
            Http::sendError(req, 422, err);

            return;
        }

        Http::sendOk(req);
    }
}  // namespace Lightnet
