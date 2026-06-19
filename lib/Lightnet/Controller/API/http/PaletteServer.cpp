#include "PaletteServer.hpp"
#include "HttpHelpers.hpp"
#include "HttpUrl.hpp"
#include "../../Palettes/PaletteJson.hpp"
#include <Arduino.h>
#include <string.h>
#include <stdio.h>

namespace Lightnet {
    PaletteServer::PaletteServer(
        AsyncWebServer&    _server,
        PaletteRepository& _palettes,
        AppearanceStore&   _appearance
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
            handleGetPalette(r);
        });
        server.on("/api/palettes/*", HTTP_DELETE, [this](AsyncWebServerRequest *r) {
            handleDeletePalette(r);
        });
        Http::onBody(server, "/api/palettes/*", HTTP_PUT, Http::MAX_BODY_LARGE,
                     this, &PaletteServer::handlePutPalette);
        server.on("/api/palettes", HTTP_GET, [this](AsyncWebServerRequest *r) {
            handleListPalettes(r);
        });
        Http::onBody(server, "/api/palettes", HTTP_POST, Http::MAX_BODY_LARGE,
                     this, &PaletteServer::handlePostPalette);
    }

    int PaletteServer::serializePaletteJson(const PaletteRecord& record, char *buf, size_t bufLen)
    {
        int n = 0;

        if (record.builtin) {
            n = snprintf(buf, bufLen,
                         "{\"schemaVersion\":1,\"name\":\"%s\",\"builtin\":true",
                         record.name);
        } else {
            n = snprintf(buf, bufLen,
                         "{\"schemaVersion\":1,\"name\":\"%s\"",
                         record.name);
        }

        if (record.stopsCount == 0) {
            if (n > 0 && n < (int)bufLen - 1) {
                buf[n++] = '}';
                buf[n]   = '\0';
            }

            return n;
        }

        if (n > 0 && n + 10 < (int)bufLen) {
            n += snprintf(buf + n, bufLen - n, ",\"stops\":[");
        }

        for (uint8_t i = 0; i < record.stopsCount && n + 24 < (int)bufLen; i++) {
            char hex[8];

            snprintf(hex, sizeof(hex), "#%02X%02X%02X",
                     record.stops[i].r, record.stops[i].g, record.stops[i].b);
            n += snprintf(buf + n, bufLen - n,
                          "%s[%u,\"%s\"]", i ? "," : "", (unsigned)record.stops[i].pos, hex);
        }

        if (n > 0 && n + 2 < (int)bufLen) {
            n += snprintf(buf + n, bufLen - n, "]}");
        }

        return n;
    }

    void PaletteServer::handleListPalettes(AsyncWebServerRequest *req)
    {
        AsyncResponseStream *res = req->beginResponseStream("application/json");

        res->print("[");

        struct ListState {
            AsyncResponseStream *res;
            AppearanceStore *    appearance;
            bool                 first;
        } state = { res, &appearance, true };

        palettes.foreachRecord(
            [](const PaletteRecord& src, void *ctx) {
            auto *s = static_cast<ListState *>(ctx);

            PaletteRecord record = src;

            if (record.stopsCount == 0 &&
                paletteNamesEqual(record.name, USER_COLORS_PALETTE_NAME)) {
                buildUserColors(s->appearance->baseColors(), record.stops, record.stopsCount);
            }

            char buf[512];

            PaletteServer::serializePaletteJson(record, buf, sizeof(buf));

            if (!s->first) s->res->print(",");

            s->res->print(buf);
            s->first = false;
        },
            &state);

        res->print("]");

        req->send(res);
    }

    void PaletteServer::handleGetPalette(AsyncWebServerRequest *req)
    {
        char name[MAX_PALETTE_NAME_LENGTH + 1];

        if (!Http::decodedNameFromUrl(req->url().c_str(), "/api/palettes/", name, sizeof(name))) {
            Http::sendError(req, 400, "invalid_name");

            return;
        }

        if (!palettes.exists(name)) {
            Http::sendError(req, 404, "not_found");

            return;
        }

        PaletteRecord record = {};

        if (paletteNamesEqual(name, USER_COLORS_PALETTE_NAME)) {
            strncpy(record.name, USER_COLORS_PALETTE_NAME, sizeof(record.name) - 1);
            record.builtin = true;
            buildUserColors(appearance.baseColors(), record.stops, record.stopsCount);
        } else if (palettes.get(name, record) != PALETTE_STORE_OK) {
            Http::sendError(req, 404, "not_found");

            return;
        }

        char buf[512];

        serializePaletteJson(record, buf, sizeof(buf));
        Http::sendOkJson(req, buf);
    }

    void PaletteServer::handlePostPalette(AsyncWebServerRequest *req, const uint8_t *body, size_t len)
    {
        GradientStop stops[PALETTE_STOPS];
        uint8_t count = 0;
        char name[MAX_PALETTE_NAME_LENGTH + 1] = {};

        if (!parsePaletteJson((const char *)body, len, stops, count, name, sizeof(name))) {
            Http::sendError(req, 422, "invalid_palette_json");

            return;
        }

        PaletteRecord record = {};

        strncpy(record.name, name, sizeof(record.name) - 1);
        record.builtin    = false;
        record.stopsCount = count;

        for (uint8_t i = 0; i < count; i++) record.stops[i] = stops[i];

        PaletteStoreResult result = palettes.create(record);

        if (result == PALETTE_STORE_NAME_EXISTS) {
            Http::sendError(req, 409, "name_exists");

            return;
        }

        if (result != PALETTE_STORE_OK) {
            Http::sendError(req, 500, "store_error");

            return;
        }

        char resp[MAX_PALETTE_NAME_LENGTH + 16];

        snprintf(resp, sizeof(resp), "{\"name\":\"%s\"}", name);
        Http::sendOkJson(req, resp);
    }

    void PaletteServer::handlePutPalette(AsyncWebServerRequest *req, const uint8_t *body, size_t len)
    {
        char name[MAX_PALETTE_NAME_LENGTH + 1];

        if (!Http::decodedNameFromUrl(req->url().c_str(), "/api/palettes/", name, sizeof(name))) {
            Http::sendError(req, 400, "invalid_name");

            return;
        }

        if (palettes.isBuiltin(name)) {
            Http::sendError(req, 403, "cannot_overwrite_builtin");

            return;
        }

        GradientStop stops[PALETTE_STOPS];
        uint8_t count = 0;

        if (!parsePaletteJson((const char *)body, len, stops, count)) {
            Http::sendError(req, 422, "invalid_palette_json");

            return;
        }

        PaletteRecord record = {};

        strncpy(record.name, name, sizeof(record.name) - 1);
        record.builtin    = false;
        record.stopsCount = count;

        for (uint8_t i = 0; i < count; i++) record.stops[i] = stops[i];

        PaletteStoreResult result = palettes.update(name, record);

        if (result == PALETTE_STORE_NOT_FOUND) {
            Http::sendError(req, 404, "not_found");

            return;
        }

        if (result != PALETTE_STORE_OK) {
            Http::sendError(req, 500, "store_error");

            return;
        }

        Http::sendOkJson(req, "{}");
    }

    void PaletteServer::handleDeletePalette(AsyncWebServerRequest *req)
    {
        char name[MAX_PALETTE_NAME_LENGTH + 1];

        if (!Http::decodedNameFromUrl(req->url().c_str(), "/api/palettes/", name, sizeof(name))) {
            Http::sendError(req, 400, "invalid_name");

            return;
        }

        if (palettes.isBuiltin(name)) {
            Http::sendError(req, 403, "cannot_delete_builtin");

            return;
        }

        PaletteStoreResult result = palettes.remove(name);

        if (result == PALETTE_STORE_NOT_FOUND) {
            Http::sendError(req, 404, "not_found");

            return;
        }

        if (result != PALETTE_STORE_OK) {
            Http::sendError(req, 500, "store_error");

            return;
        }

        req->send(204);
    }
}  // namespace Lightnet
