#include "PanelServer.hpp"
#include "HttpHelpers.hpp"
#include "../../../Utils/SimpleJson.hpp"
#include "../../../Common/Protocol.hpp"
#include "../../Panels/PanelsInitializer.hpp"
#include "../../Panels/Panel.hpp"
#include "../../Panels/Edge.hpp"
#include <Arduino.h>
#include <string.h>
#include <stdlib.h>

namespace Lightnet {
    namespace {
        // Parse /api/panels/<addr>/<action> — returns false on malformed URL.
        bool parseAddrAction(const char *url, uint8_t *addrOut, char *actionOut, size_t actionLen)
        {
            const char *prefix = "/api/panels/";
            size_t pfxLen = strlen(prefix);

            if (strncmp(url, prefix, pfxLen) != 0) return false;

            const char *p = url + pfxLen;

            if (*p < '0' || *p > '9') return false;

            long addr = 0;

            while (*p >= '0' && *p <= '9') {
                addr = addr * 10 + (*p++ - '0');

                if (addr > 255) return false;
            }

            if (*p != '/') return false;

            p++;

            size_t alen = strlen(p);

            if (alen == 0 || alen >= actionLen) return false;

            memcpy(actionOut, p, alen + 1);
            *addrOut = (uint8_t)addr;

            return true;
        }
    } // anonymous namespace

    PanelServer::PanelServer(
        AsyncWebServer&   _server,
        PanelsController& _panelsController
    )
        : server(_server), panelsController(_panelsController)
    {
    }

    void PanelServer::begin()
    {
        registerRoutes();
    }

    void PanelServer::registerRoutes()
    {
        // Specific routes must be registered before the wildcard.
        server.on("/api/panels", HTTP_GET, [this](AsyncWebServerRequest *r) {
            handleGetPanels(r);
        });
        server.on("/api/panels/edges", HTTP_GET, [this](AsyncWebServerRequest *r) {
            handleGetEdges(r);
        });
        Http::onBody(server, "/api/panels/*", HTTP_PUT, Http::MAX_BODY_SMALL,
                     this, &PanelServer::handlePutPanel);
    }

    void PanelServer::handleGetPanels(AsyncWebServerRequest *req)
    {
        List<Panel *> *panels = LNPanelsInitializer.getPanels();
        AsyncResponseStream *response = req->beginResponseStream("application/json");

        response->print("[");

        bool first = true;

        for (uint16_t i = 0; i < panels->getSize(); i++) {
            Panel *panel = panels->get(i);
            Protocol::PanelState state;

            if (panelsController.fetchState(panel->index, &state)) continue;

            char hex[8];

            jsonFormatHex(state.color.r, state.color.g, state.color.b, hex);

            char entry[80];

            snprintf(entry, sizeof(entry),
                     "%s{\"address\":%u,\"on\":%s,\"color\":\"%s\",\"brightness\":%u}",
                     first ? "" : ",",
                     (unsigned)panel->index,
                     state.state ? "true" : "false",
                     hex,
                     (unsigned)state.brightness);
            response->print(entry);
            first = false;
        }

        response->print("]");
        req->send(response);
    }

    void PanelServer::handleGetEdges(AsyncWebServerRequest *req)
    {
        List<Panel *> *panels = LNPanelsInitializer.getPanels();
        AsyncResponseStream *response = req->beginResponseStream("application/json");

        response->print("[");

        bool first = true;

        for (uint16_t pi = 0; pi < panels->getSize(); pi++) {
            Panel *panel = panels->get(pi);

            for (uint16_t ei = 0; ei < panel->edges->getSize(); ei++) {
                Edge *edge = panel->edges->get(ei);
                uint16_t connPanel = edge->connectedEdge ? edge->connectedEdge->panel->index : 0;
                uint16_t connEdge  = edge->connectedEdge ? edge->connectedEdge->index : 0;

                char entry[96];

                snprintf(entry, sizeof(entry),
                         "%s{\"panel\":%u,\"edge\":%u,\"connectedPanel\":%u,\"connectedEdge\":%u}",
                         first ? "" : ",",
                         (unsigned)panel->index,
                         (unsigned)edge->index,
                         (unsigned)connPanel,
                         (unsigned)connEdge);
                response->print(entry);
                first = false;
            }
        }

        response->print("]");
        req->send(response);
    }

    void PanelServer::handlePutPanel(AsyncWebServerRequest *req, const uint8_t *body, size_t len)
    {
        uint8_t addr;
        char action[16];

        if (!parseAddrAction(req->url().c_str(), &addr, action, sizeof(action))) {
            Http::sendError(req, 400, "bad_url");

            return;
        }

        SimpleJson j(body, len);

        if (strcmp(action, "on") == 0) {
            long v = j.getInt("value");

            if (v < 0 || v > 1) {
                Http::sendError(req, 422, "value_must_be_0_or_1");

                return;
            }

            panelsController.turnOnOff(addr, (uint8_t)v);
            Http::sendOk(req);
        } else if (strcmp(action, "brightness") == 0) {
            long v = j.getInt("value");

            if (v < 0 || v > 255) {
                Http::sendError(req, 422, "value_out_of_range");

                return;
            }

            panelsController.setBrightness(addr, (uint8_t)v);
            Http::sendOk(req);
        } else if (strcmp(action, "color") == 0) {
            char hex[16];

            if (!j.getString("color", hex, sizeof(hex))) {
                Http::sendError(req, 422, "missing_color");

                return;
            }

            uint8_t r, g, b;

            if (!jsonParseHexColor(hex, strlen(hex), &r, &g, &b)) {
                Http::sendError(req, 422, "bad_hex_color");

                return;
            }

            Protocol::Color color;

            color.rgb = { r, g, b };
            panelsController.setColor(addr, color);
            Http::sendOk(req);
        } else {
            Http::sendError(req, 404, "not_found");
        }
    }
}  // namespace Lightnet
