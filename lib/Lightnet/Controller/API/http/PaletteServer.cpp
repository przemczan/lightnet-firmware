#include "PaletteServer.hpp"
#include "HttpHelpers.hpp"
#include "HttpJsonCapacity.hpp"
#include "HttpUrl.hpp"
#include "../../Palettes/PaletteJson.hpp"
#include "../../../Utils/SimpleJson.hpp"
#include <Arduino.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

namespace Lightnet {
    PaletteServer::PaletteServer(
        AsyncWebServer&    _server,
        PaletteRepository& _palettes,
        AppearanceService& _appearance
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
        Http::onRequest(server, "/api/palettes/*", HTTP_GET, this, &PaletteServer::handleGetPalette);
        Http::onRequest(server, "/api/palettes/*", HTTP_DELETE, this, &PaletteServer::handleDeletePalette);
        Http::onBody(server, "/api/palettes/*", HTTP_PUT, Http::MAX_BODY_LARGE,
                     this, &PaletteServer::handlePutPalette);
        Http::onRequest(server, "/api/palettes", HTTP_GET, this, &PaletteServer::handleListPalettes);
        Http::onBody(server, "/api/palettes", HTTP_POST, Http::MAX_BODY_LARGE,
                     this, &PaletteServer::handlePostPalette);
    }

    int PaletteServer::serializePaletteJson(const PaletteRecord& record, char *buf, size_t bufLen)
    {
        if (!buf || bufLen < 32) return -1;

        size_t pos = (size_t)snprintf(buf, bufLen, "{\"schemaVersion\":1,\"name\":");

        if (pos >= bufLen) return -1;

        pos = jsonAppendQuotedString(buf, bufLen, pos, record.name);

        if (pos == (size_t)-1) return -1;

        if (record.builtin) {
            static const char BUILTIN_TRUE[] = ",\"builtin\":true";
            constexpr size_t builtinLen = sizeof(BUILTIN_TRUE) - 1;

            if (pos + builtinLen >= bufLen) return -1;

            memcpy(buf + pos, BUILTIN_TRUE, builtinLen);
            pos += builtinLen;
        }

        if (record.stopsCount == 0) {
            if (pos + 1 >= bufLen) return -1;

            buf[pos++] = '}';
            buf[pos]   = '\0';

            return (int)pos;
        }

        static const char STOPS_FIELD[] = ",\"stops\":[";
        constexpr size_t stopsFieldLen = sizeof(STOPS_FIELD) - 1;

        if (pos + stopsFieldLen >= bufLen) return -1;

        memcpy(buf + pos, STOPS_FIELD, stopsFieldLen);
        pos += stopsFieldLen;

        for (uint8_t i = 0; i < record.stopsCount; i++) {
            char hex[8];

            snprintf(hex, sizeof(hex), "#%02X%02X%02X",
                     record.stops[i].r, record.stops[i].g, record.stops[i].b);

            int n = snprintf(buf + pos, bufLen - pos, "%s[%u,\"%s\"]",
                             i ? "," : "", (unsigned)record.stops[i].pos, hex);

            if (n <= 0 || pos + (size_t)n >= bufLen) return -1;

            pos += (size_t)n;
        }

        if (pos + HttpJson::CLOSE_RESERVE >= bufLen) return -1;

        buf[pos++] = ']';
        buf[pos++] = '}';
        buf[pos]   = '\0';

        return (int)pos;
    }

    namespace {
        struct ListPalettesContext {
            char * buf;
            size_t cap;
            size_t pos;
            bool   first;
            bool   ok;
        };

        bool appendListEntry(ListPalettesContext *ctx, const PaletteRecord& record)
        {
            if (!ctx->ok) return false;

            if (!ctx->first) {
                if (ctx->pos + 1 >= ctx->cap) {
                    ctx->ok = false;

                    return false;
                }

                ctx->buf[ctx->pos++] = ',';
            }

            int n = PaletteServer::serializePaletteJson(record, ctx->buf + ctx->pos, ctx->cap - ctx->pos);

            if (n <= 0) {
                ctx->ok = false;

                return false;
            }

            ctx->pos   += (size_t)n;
            ctx->first  = false;

            return true;
        }

        void foreachListEntry(const PaletteRecord& record, void *userContext)
        {
            auto *ctx = static_cast<ListPalettesContext *>(userContext);

            if (paletteNamesEqual(record.name, USER_COLORS_PALETTE_NAME)) return;

            appendListEntry(ctx, record);
        }
    } // namespace

    void PaletteServer::handleListPalettes(AsyncWebServerRequest *req)
    {
        size_t cap = HttpJson::paletteListCapacity(palettes.count());

        char *buf = (char *)malloc(cap);

        if (!buf) {
            Http::sendError(req, 500, "oom");

            return;
        }

        ListPalettesContext ctx = { buf, cap, 0, true, true };

        buf[ctx.pos++] = '[';

        PaletteRecord userColors = {};

        strncpy(userColors.name, USER_COLORS_PALETTE_NAME, sizeof(userColors.name) - 1);
        userColors.builtin = true;
        buildUserColors(appearance.baseColors(), userColors.stops, userColors.stopsCount);
        appendListEntry(&ctx, userColors);

        palettes.foreachRecord(foreachListEntry, &ctx);

        if (!ctx.ok || ctx.pos + HttpJson::CLOSE_RESERVE > ctx.cap) {
            free(buf);
            Http::sendError(req, 500, "response_overflow");

            return;
        }

        buf[ctx.pos++] = ']';
        buf[ctx.pos]   = '\0';

        Http::sendOkJson(req, buf);
        free(buf);
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

        char buf[HttpJson::PALETTE_GET_BUFFER];

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

        char resp[HttpJson::PALETTE_CREATE_BUFFER];

        if (jsonWriteObjectStringField(resp, sizeof(resp), "name", name) < 0) {
            Http::sendError(req, 500, "response_overflow");

            return;
        }

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

        Http::sendNoContent(req);
    }
}  // namespace Lightnet
