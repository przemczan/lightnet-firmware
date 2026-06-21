#include "PaletteServer.hpp"
#include "HttpHelpers.hpp"
#include "HttpUrl.hpp"
#include "../../Palettes/PaletteJson.hpp"
#include <Arduino.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

namespace Lightnet {
    PaletteServer::PaletteServer(
        AsyncWebServer&    _server,
        PaletteRepository& _palettes,
        AppearanceService&   _appearance
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

    namespace {
        // Heap state for the chunked /api/palettes listing. Owned solely via
        // req->_tempObject + req->onDisconnect (see handleListPalettes) — freed
        // exactly once, never by the fill callback itself.
        struct ListPalettesState : Http::detail::RequestContext {
            AppearanceService *  appearance;
            PaletteRepository *palettes;
            size_t             cursor;  // next DB slot offset to scan from
            bool               emittedOpenBracket;
            bool               emittedUserColors;
            bool               first;   // controls comma placement
            bool               dbExhausted;
            bool               emittedCloseBracket;
            char               pending[600]; // one JSON piece: bracket, or one record + separator
            size_t             pendingLen;
            size_t             pendingPos; // read cursor into pending
        };

        // Serializes one piece of JSON (with leading comma if needed) into
        // state->pending, resetting the pending read cursor to the start.
        void appendPendingRecord(ListPalettesState *state, const PaletteRecord& record)
        {
            size_t prefixLen = 0;

            if (!state->first) {
                state->pending[0] = ',';
                prefixLen = 1;
            }

            int n = PaletteServer::serializePaletteJson(
                record, state->pending + prefixLen, sizeof(state->pending) - prefixLen);

            state->pendingLen = prefixLen + ((n > 0) ? (size_t)n : 0);
            state->pendingPos = 0;
            state->first      = false;
        }

        size_t paletteListFill(ListPalettesState *state, uint8_t *buf, size_t maxLen)
        {
            size_t written = 0;

            while (written < maxLen) {
                if (state->pendingPos < state->pendingLen) {
                    size_t n = state->pendingLen - state->pendingPos;

                    if (n > maxLen - written) n = maxLen - written;

                    memcpy(buf + written, state->pending + state->pendingPos, n);
                    state->pendingPos += n;
                    written           += n;

                    continue;
                }

                if (!state->emittedOpenBracket) {
                    state->pending[0]  = '[';
                    state->pendingLen  = 1;
                    state->pendingPos  = 0;
                    state->emittedOpenBracket = true;

                    continue;
                }

                if (!state->emittedUserColors) {
                    PaletteRecord record = {};

                    strncpy(record.name, USER_COLORS_PALETTE_NAME, sizeof(record.name) - 1);
                    record.builtin = true;
                    buildUserColors(state->appearance->baseColors(), record.stops, record.stopsCount);

                    appendPendingRecord(state, record);
                    state->emittedUserColors = true;

                    continue;
                }

                if (!state->dbExhausted) {
                    PaletteRecord record = {};
                    size_t nextCursor = state->cursor;
                    bool found = false;

                    PaletteStoreResult result =
                        state->palettes->nextRecord(state->cursor, record, nextCursor, found);

                    state->cursor = nextCursor;

                    if (result != PALETTE_STORE_OK || !found) {
                        state->dbExhausted = true;

                        continue;
                    }

                    appendPendingRecord(state, record);

                    continue;
                }

                if (!state->emittedCloseBracket) {
                    state->pending[0]  = ']';
                    state->pendingLen  = 1;
                    state->pendingPos  = 0;
                    state->emittedCloseBracket = true;

                    continue;
                }

                break;
            }

            return written;
        }
    } // namespace

    void PaletteServer::handleListPalettes(AsyncWebServerRequest *req)
    {
        auto *state = (ListPalettesState *)malloc(sizeof(ListPalettesState));

        if (!state) {
            Http::sendError(req, 500, "oom");

            return;
        }

        if (req->_tempObject) {
            state->startMs = static_cast<Http::detail::RequestContext *>(req->_tempObject)->startMs;
            free(req->_tempObject);
        } else {
            state->startMs = millis();
        }

        state->appearance          = &appearance;
        state->palettes            = &palettes;
        state->cursor              = RECORDS_START_OFFSET;
        state->emittedOpenBracket  = false;
        state->emittedUserColors   = false;
        state->first               = true;
        state->dbExhausted         = false;
        state->emittedCloseBracket = false;
        state->pendingLen          = 0;
        state->pendingPos          = 0;

        req->_tempObject = state;
        Http::onDisconnectLogged(req, [req]() {
            if (req->_tempObject) {
                free(req->_tempObject);
                req->_tempObject = nullptr;
            }
        });

        AsyncWebServerResponse *res = req->beginChunkedResponse(
            "application/json",
            [state, req](uint8_t *buf, size_t maxLen, size_t /*index*/) -> size_t {
            size_t written = paletteListFill(state, buf, maxLen);

            Http::logFillTick(req, written, maxLen);

            if (written == 0) Http::logChunkedComplete(req);

            return written;
        });

        Http::sendOkChunked(req, res);
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

        Http::sendNoContent(req);
    }
}  // namespace Lightnet
