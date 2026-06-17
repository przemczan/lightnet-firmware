#include "PaletteServer.hpp"
#include "HttpHelpers.hpp"
#include "../../Palettes/PaletteJson.hpp"
#include "../../../Utils/SimpleJson.hpp"
#include <Arduino.h>
#include "../../../Utils/Fs/Fs.hpp"
#include <string.h>
#include <stdio.h>

namespace Lightnet {
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

    void PaletteServer::registerRoutes()
    {
        server.on("/api/palettes/*", HTTP_GET, [this](AsyncWebServerRequest *r) {
            handleGetPaletteByName(r);
        });
        server.on("/api/palettes/*", HTTP_DELETE, [this](AsyncWebServerRequest *r) {
            handleDeletePalette(r);
        });
        server.on("/api/palettes", HTTP_GET, [this](AsyncWebServerRequest *r) {
            handleListPalettes(r);
        });
        Http::onBody(server, "/api/palettes", HTTP_POST, Http::MAX_BODY_LARGE,
                     this, &PaletteServer::handlePostPalette);
    }

    void PaletteServer::handleListPalettes(AsyncWebServerRequest *req)
    {
        AsyncResponseStream *res = req->beginResponseStream("application/json");
        GradientStop stops[PALETTE_STOPS];
        uint8_t count;
        bool first = true;
        char buf[128];

        auto writeEntry = [&](const char *name) {
                              if (!first) res->print(",");

                              first = false;
                              snprintf(buf, sizeof(buf), "\"%s\":{\"schemaVersion\":1,\"name\":\"%s\",\"stops\":[", name, name);
                              res->print(buf);

                              for (uint8_t i = 0; i < count; i++) {
                                  char hex[8];
                                  jsonFormatHex(stops[i].r, stops[i].g, stops[i].b, hex);
                                  snprintf(buf, sizeof(buf), "%s[%u,\"%s\"]", i ? "," : "", (unsigned)stops[i].pos, hex);
                                  res->print(buf);
                              }

                              res->print("]}");
                          };

        res->print("{");

        count = 0;
        PaletteStore::buildUserColors(appearance.baseColors(), stops, count);
        writeEntry("userColors");

        for (uint8_t i = 0; i < palettes.builtInCount(); i++) {
            const char *name = palettes.builtInName(i);

            count = 0;

            if (palettes.resolve(name, stops, count)) writeEntry(name);
        }

        FsDir d("/palettes/");

        while (d.next()) {
            String fn = d.fileName();
            const char *base = fn.c_str();

            if (strncmp(base, "/palettes/", 10) == 0) base += 10;

            size_t blen = strlen(base);

            if (blen > 5 && strcmp(base + blen - 5, ".json") == 0) {
                char name[24] = { 0 };
                size_t nlen = blen - 5;

                if (nlen < sizeof(name) && !palettes.isBuiltIn(name)) {
                    memcpy(name, base, nlen);
                    count = 0;

                    if (palettes.resolve(name, stops, count)) writeEntry(name);
                }
            }
        }

        res->print("}");
        req->send(res);
    }

    void PaletteServer::handleGetPaletteByName(AsyncWebServerRequest *req)
    {
        char name[24];

        if (!Http::nameFromUrl(req->url().c_str(), "/api/palettes/", name, sizeof(name)) ||
            !Http::isSafeName(name)) {
            Http::sendError(req, 400, "invalid_name");

            return;
        }

        GradientStop stops[PALETTE_STOPS];
        uint8_t count = 0;

        if (strcmp(name, "userColors") == 0) {
            PaletteStore::buildUserColors(appearance.baseColors(), stops, count);
        } else if (!palettes.resolve(name, stops, count)) {
            Http::sendError(req, 404, "not_found");

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
        Http::sendOkJson(req, buf);
    }

    void PaletteServer::handlePostPalette(AsyncWebServerRequest *req, const uint8_t *body, size_t len)
    {
        GradientStop stops[PALETTE_STOPS];
        uint8_t count = 0;
        char name[20] = { 0 };

        if (!parsePaletteJson((const char *)body, len, stops, count, name, sizeof(name)) ||
            !Http::isSafeName(name)) {
            Http::sendError(req, 422, "invalid_palette_json");

            return;
        }

        if (palettes.isBuiltIn(name) || strcmp(name, "userColors") == 0) {
            Http::sendError(req, 403, "cannot_overwrite_builtin");

            return;
        }

        if (!palettes.save(name, stops, count)) {
            Http::sendError(req, 500, "fs_write_failed");

            return;
        }

        char resp[48];

        snprintf(resp, sizeof(resp), "{\"saved\":\"%s\"}", name);
        Http::sendOkJson(req, resp);
    }

    void PaletteServer::handleDeletePalette(AsyncWebServerRequest *req)
    {
        char name[24];

        if (!Http::nameFromUrl(req->url().c_str(), "/api/palettes/", name, sizeof(name)) ||
            !Http::isSafeName(name)) {
            Http::sendError(req, 400, "invalid_name");

            return;
        }

        if (palettes.isBuiltIn(name) || strcmp(name, "userColors") == 0) {
            Http::sendError(req, 403, "cannot_delete_builtin");

            return;
        }

        if (!palettes.deleteUserPalette(name)) {
            Http::sendError(req, 404, "not_found");

            return;
        }

        Http::sendOk(req);
    }
}  // namespace Lightnet
