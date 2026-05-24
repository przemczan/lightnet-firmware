#include "PanelServer.hpp"
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
        struct BodyBuf {
            size_t  len, cap;
            uint8_t data[1];
        };

        bool appendBody(AsyncWebServerRequest *req, const uint8_t *data, size_t len, size_t total, size_t maxCap)
        {
            BodyBuf *buf = (BodyBuf *)req->_tempObject;

            if (!buf) {
                size_t cap = (total > 0) ? total : 64;

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

    void PanelServer::sendOk(AsyncWebServerRequest *req)
    {
        req->send(200, "application/json", "{\"ok\":true}");
    }

    void PanelServer::sendOkJson(AsyncWebServerRequest *req, const char *json)
    {
        req->send(200, "application/json", json);
    }

    void PanelServer::sendError(AsyncWebServerRequest *req, int code, const char *msg)
    {
        char buf[128];

        snprintf(buf, sizeof(buf), "{\"error\":\"%s\"}", msg ? msg : "error");
        req->send(code, "application/json", buf);
    }

    void PanelServer::registerRoutes()
    {
        auto bodySmall = [this](AsyncWebServerRequest *r, uint8_t *d, size_t l, size_t i, size_t t) {
                             if (!appendBody(r, d, l, t, MAX_BODY_SMALL)) sendError(r, 413, "body_too_large");
                         };
        auto dispatchSmall = [this](void (PanelServer::*m)(AsyncWebServerRequest *, const uint8_t *, size_t)) {
                                 return [this, m](AsyncWebServerRequest *r) {
                                            BodyBuf *buf = (BodyBuf *)r->_tempObject;

                                            if (!buf) {
                                                sendError(r, 400, "empty_body");

                                                return;
                                            }

                                            (this->*m)(r, buf->data, buf->len);
                                 };
                             };

        // Specific routes must be registered before the wildcard.
        server.on("/api/panels", HTTP_GET, [this](AsyncWebServerRequest *r) {
            handleGetPanels(r);
        });
        server.on("/api/panels/edges", HTTP_GET, [this](AsyncWebServerRequest *r) {
            handleGetEdges(r);
        });
        server.on("/api/panels/*", HTTP_PUT, dispatchSmall(&PanelServer::handlePutPanel), nullptr, bodySmall);
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
            sendError(req, 400, "bad_url");

            return;
        }

        SimpleJson j(body, len);

        if (strcmp(action, "on") == 0) {
            long v = j.getInt("value");

            if (v < 0 || v > 1) {
                sendError(req, 422, "value_must_be_0_or_1");

                return;
            }

            panelsController.turnOnOff(addr, (uint8_t)v);
            sendOk(req);
        } else if (strcmp(action, "brightness") == 0) {
            long v = j.getInt("value");

            if (v < 0 || v > 255) {
                sendError(req, 422, "value_out_of_range");

                return;
            }

            panelsController.setBrightness(addr, (uint8_t)v);
            sendOk(req);
        } else if (strcmp(action, "color") == 0) {
            char hex[16];

            if (!j.getString("color", hex, sizeof(hex))) {
                sendError(req, 422, "missing_color");

                return;
            }

            uint8_t r, g, b;

            if (!jsonParseHexColor(hex, strlen(hex), &r, &g, &b)) {
                sendError(req, 422, "bad_hex_color");

                return;
            }

            Protocol::Color color;

            color.rgb = { r, g, b };
            panelsController.setColor(addr, color);
            sendOk(req);
        } else {
            sendError(req, 404, "not_found");
        }
    }
}  // namespace Lightnet
