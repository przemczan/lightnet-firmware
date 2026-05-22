#include "PaletteServer.hpp"
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

    PaletteServer::PaletteServer(
        AsyncWebServer&  _server,
        PaletteStore&    _palettes,
        AppearanceStore& _appearance
    )
        : server(_server), palettes(_palettes), appearance(_appearance)
    {
    }

    void PaletteServer::begin()
    {
        registerRoutes();
    }

    void PaletteServer::sendOk(AsyncWebServerRequest *req)
    {
        req->send(200, "application/json", "{\"ok\":true}");
    }

    void PaletteServer::sendOkJson(AsyncWebServerRequest *req, const char *json)
    {
        req->send(200, "application/json", json);
    }

    void PaletteServer::sendError(AsyncWebServerRequest *req, int code, const char *msg)
    {
        char buf[128];

        snprintf(buf, sizeof(buf), "{\"error\":\"%s\"}", msg ? msg : "error");
        req->send(code, "application/json", buf);
    }

    void PaletteServer::registerRoutes()
    {
        auto bodyLarge = [this](AsyncWebServerRequest *r, uint8_t *d, size_t l, size_t i, size_t t) {
                             if (!appendBody(r, d, l, t, MAX_BODY_LARGE)) sendError(r, 413, "body_too_large");
                         };

        auto dispatchLarge = [this](void (PaletteServer::*m)(AsyncWebServerRequest *, const uint8_t *, size_t)) {
                                 return [this, m](AsyncWebServerRequest *r) {
                                            BodyBuf *buf = (BodyBuf *)r->_tempObject;

                                            if (!buf) {
                                                sendError(r, 400, "empty_body");

                                                return;
                                            }

                                            (this->*m)(r, buf->data, buf->len);
                                 };
                             };

        server.on("/api/palettes", HTTP_GET, [this](AsyncWebServerRequest *r){
            handleListPalettes(r);
        });
        server.on("/api/palettes", HTTP_POST, dispatchLarge(&PaletteServer::handlePostPalette), nullptr, bodyLarge);
        server.on("/api/palettes/*", HTTP_GET, [this](AsyncWebServerRequest *r){
            handleGetPaletteByName(r);
        });
        server.on("/api/palettes/*", HTTP_DELETE, [this](AsyncWebServerRequest *r){
            handleDeletePalette(r);
        });
    }

    void PaletteServer::handleListPalettes(AsyncWebServerRequest *req)
    {
        char buf[512];
        int n = snprintf(buf, sizeof(buf), "[\"userColors\"");

        for (uint8_t i = 0; i < palettes.builtInCount() && n + 32 < (int)sizeof(buf); i++) {
            n += snprintf(buf + n, sizeof(buf) - n, ",\"%s\"", palettes.builtInName(i));
        }

        Dir d = SPIFFS.openDir("/palettes/");

        while (d.next() && n + 32 < (int)sizeof(buf)) {
            String fn = d.fileName();
            const char *base = fn.c_str();

            if (strncmp(base, "/palettes/", 10) == 0) base += 10;

            size_t blen = strlen(base);

            if (blen > 5 && strcmp(base + blen - 5, ".json") == 0) {
                char name[24] = { 0 };
                size_t nlen = blen - 5;

                if (nlen < sizeof(name) && !palettes.isBuiltIn(name)) {
                    memcpy(name, base, nlen);
                    n += snprintf(buf + n, sizeof(buf) - n, ",\"%s\"", name);
                }
            }
        }

        snprintf(buf + n, sizeof(buf) - n, "]");
        sendOkJson(req, buf);
    }

    void PaletteServer::handleGetPaletteByName(AsyncWebServerRequest *req)
    {
        char name[24];

        if (!nameFromUrl(req->url().c_str(), "/api/palettes/", name, sizeof(name)) || !isSafeName(name)) {
            sendError(req, 400, "invalid_name");

            return;
        }

        GradientStop stops[PALETTE_STOPS];
        uint8_t count = 0;

        if (strcmp(name, "userColors") == 0) {
            PaletteStore::buildUserColors(appearance.baseColors(), stops, count);
        } else if (!palettes.resolve(name, stops, count)) {
            sendError(req, 404, "not_found");

            return;
        }

        char buf[512];
        int n = snprintf(buf, sizeof(buf), "{\"schemaVersion\":1,\"name\":\"%s\",\"stops\":[", name);

        for (uint8_t i = 0; i < count && n + 32 < (int)sizeof(buf); i++) {
            char hex[8];

            jsonFormatHex(stops[i].r, stops[i].g, stops[i].b, hex);
            n += snprintf(buf + n, sizeof(buf) - n, "%s[%u,\"%s\"]", i ? "," : "", (unsigned)stops[i].pos, hex);
        }

        snprintf(buf + n, sizeof(buf) - n, "]}");
        sendOkJson(req, buf);
    }

    void PaletteServer::handlePostPalette(AsyncWebServerRequest *req, const uint8_t *body, size_t len)
    {
        GradientStop stops[PALETTE_STOPS];
        uint8_t count = 0;

        if (!PaletteStore::parsePaletteJson((const char *)body, len, stops, count)) {
            sendError(req, 422, "invalid_palette_json");

            return;
        }

        SimpleJson j(body, len);
        char name[20];

        if (!j.getString("name", name, sizeof(name)) || !isSafeName(name)) {
            sendError(req, 422, "missing_or_invalid_name");

            return;
        }

        if (palettes.isBuiltIn(name) || strcmp(name, "userColors") == 0) {
            sendError(req, 403, "cannot_overwrite_builtin");

            return;
        }

        if (!palettes.save(name, stops, count)) {
            sendError(req, 500, "spiffs_write_failed");

            return;
        }

        char resp[48];

        snprintf(resp, sizeof(resp), "{\"saved\":\"%s\"}", name);
        sendOkJson(req, resp);
    }

    void PaletteServer::handleDeletePalette(AsyncWebServerRequest *req)
    {
        char name[24];

        if (!nameFromUrl(req->url().c_str(), "/api/palettes/", name, sizeof(name)) || !isSafeName(name)) {
            sendError(req, 400, "invalid_name");

            return;
        }

        if (palettes.isBuiltIn(name) || strcmp(name, "userColors") == 0) {
            sendError(req, 403, "cannot_delete_builtin");

            return;
        }

        if (!palettes.deleteUserPalette(name)) {
            sendError(req, 404, "not_found");

            return;
        }

        sendOk(req);
    }
}  // namespace Lightnet
