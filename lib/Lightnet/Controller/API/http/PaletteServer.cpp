#include "PaletteServer.hpp"
#include "HttpHelpers.hpp"
#include "../../../Utils/EntryId.hpp"
#include "../../Palettes/PaletteJson.hpp"
#include "../../Palettes/LittleFsPaletteRepository.hpp"
#include "../../Store/StoreStreamResponse.hpp"
#include "../../../Utils/SimpleJson.hpp"
#include "../../../Utils/JsonInject.hpp"
#include <Arduino.h>
#include <string.h>
#include <stdio.h>

namespace Lightnet {
    PaletteServer::PaletteServer(
        AsyncWebServer&     _server,
        IPaletteRepository& _palettes,
        AppearanceStore&    _appearance
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
            handleGetPaletteById(r);
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

    namespace {
        struct ListCtx {
            AsyncResponseStream *res;
            bool                 first;
        };

        void appendPaletteMeta(const PaletteMeta& meta, void *ctx)
        {
            auto *c = static_cast<ListCtx *>(ctx);
            char buf[160];

            if (meta.builtin) {
                snprintf(buf, sizeof(buf),
                         "%s{\"schemaVersion\":1,\"id\":\"%s\",\"name\":\"%s\",\"builtin\":true}",
                         c->first ? "" : ",", meta.id, meta.name);
            } else {
                snprintf(buf, sizeof(buf),
                         "%s{\"schemaVersion\":1,\"id\":\"%s\",\"name\":\"%s\"}",
                         c->first ? "" : ",", meta.id, meta.name);
            }

            c->res->print(buf);
            c->first = false;
        }
    } // anonymous namespace

    void PaletteServer::handleListPalettes(AsyncWebServerRequest *req)
    {
        auto *repo = static_cast<LittleFsPaletteRepository *>(&palettes);

        repo->lock().acquire();

        AsyncResponseStream *res = req->beginResponseStream("application/json");
        ListCtx ctx { res, true };

        res->print("[");
        repo->listMetasUnlocked(appendPaletteMeta, &ctx);
        res->print("]");

        StoreLock *lockPtr = &repo->lock();

        req->onDisconnect([lockPtr]() {
            lockPtr->release();
        });

        req->send(res);
    }

    void PaletteServer::handleGetPaletteById(AsyncWebServerRequest *req)
    {
        char id[ENTRY_ID_MAX + 1];

        if (!Http::idFromUrl(req->url().c_str(), "/api/palettes/", id, sizeof(id)) ||
            !Http::isSafeId(id)) {
            Http::sendError(req, 400, "invalid_id");

            return;
        }

        if (!palettes.exists(id)) {
            Http::sendError(req, 404, "not_found");

            return;
        }

        if (palettes.isUserColors(id)) {
            GradientStop stops[PALETTE_STOPS];
            uint8_t count = 0;

            IPaletteRepository::buildUserColors(appearance.baseColors(), stops, count);

            PaletteMeta meta = {};

            if (!palettes.loadMeta(id, meta)) {
                strncpy(meta.id, id, sizeof(meta.id) - 1);
                strncpy(meta.name, "Base colors", sizeof(meta.name) - 1);
            }

            char buf[512];
            int n = snprintf(buf, sizeof(buf),
                             "{\"schemaVersion\":1,\"id\":\"%s\",\"name\":\"%s\",\"stops\":[",
                             meta.id, meta.name);

            for (uint8_t i = 0; i < count && n + 32 < (int)sizeof(buf); i++) {
                char hex[8];

                jsonFormatHex(stops[i].r, stops[i].g, stops[i].b, hex);
                n += snprintf(buf + n, sizeof(buf) - n, "%s[%u,\"%s\"]", i ? "," : "", (unsigned)stops[i].pos, hex);
            }

            snprintf(buf + n, sizeof(buf) - n, "]}");
            Http::sendOkJson(req, buf);

            return;
        }

        auto *repo = static_cast<LittleFsPaletteRepository *>(&palettes);

        repo->lock().acquire();

        IContentReader *reader = palettes.openContent(id);

        if (!reader) {
            repo->lock().release();
            Http::sendError(req, 404, "not_found");

            return;
        }

        sendLockedContent(req, repo->lock(), reader);
    }

    void PaletteServer::handlePostPalette(AsyncWebServerRequest *req, const uint8_t *body, size_t len)
    {
        GradientStop stops[PALETTE_STOPS];
        uint8_t count = 0;
        char name[65] = { 0 };
        char bodyId[ENTRY_ID_MAX + 1] = { 0 };

        if (!parsePaletteJson((const char *)body, len, stops, count, name, sizeof(name)) ||
            !isValidDisplayName(name)) {
            Http::sendError(req, 422, "invalid_palette_json");

            return;
        }

        bool hasId = jsonReadTopLevelString((const char *)body, len, "id", bodyId, sizeof(bodyId));
        bool updating = hasId && Http::isSafeId(bodyId) && palettes.exists(bodyId);

        if (updating) {
            if (palettes.isBuiltIn(bodyId) || palettes.isUserColors(bodyId)) {
                Http::sendError(req, 403, "cannot_overwrite_builtin");

                return;
            }

            if (!palettes.update(bodyId, name, stops, count)) {
                Http::sendError(req, 500, "fs_write_failed");

                return;
            }

            char resp[48];

            snprintf(resp, sizeof(resp), "{\"id\":\"%s\"}", bodyId);
            Http::sendOkJson(req, resp);

            return;
        }

        char newId[ENTRY_ID_MAX + 1] = { 0 };

        if (!palettes.saveNew(name, stops, count, newId, sizeof(newId))) {
            Http::sendError(req, 500, "fs_write_failed");

            return;
        }

        char resp[48];

        snprintf(resp, sizeof(resp), "{\"id\":\"%s\"}", newId);
        Http::sendOkJson(req, resp);
    }

    void PaletteServer::handleDeletePalette(AsyncWebServerRequest *req)
    {
        char id[ENTRY_ID_MAX + 1];

        if (!Http::idFromUrl(req->url().c_str(), "/api/palettes/", id, sizeof(id)) ||
            !Http::isSafeId(id)) {
            Http::sendError(req, 400, "invalid_id");

            return;
        }

        if (palettes.isBuiltIn(id) || palettes.isUserColors(id)) {
            Http::sendError(req, 403, "cannot_delete_builtin");

            return;
        }

        if (!palettes.deleteEntry(id)) {
            Http::sendError(req, 404, "not_found");

            return;
        }

        Http::sendOk(req);
    }
}  // namespace Lightnet
