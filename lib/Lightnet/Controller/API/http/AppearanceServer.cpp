#include "AppearanceServer.hpp"
#include "HttpHelpers.hpp"
#include "../../../Utils/SimpleJson.hpp"
#include <Arduino.h>
#include <string.h>
#include <stdio.h>

namespace Lightnet {
    AppearanceServer::AppearanceServer(
        AsyncWebServer&    _server,
        AppearanceStore&   _appearance,
        PaletteRepository& _palettes,
        ScenesService&     _animService,
        MainLoopQueue&     _queue
    )
        : server(_server), appearance(_appearance), palettes(_palettes), animService(_animService),
        queue(_queue)
    {
    }

    void AppearanceServer::begin()
    {
        registerRoutes();
    }

    void AppearanceServer::registerRoutes()
    {
        server.on("/api/appearance", HTTP_GET, [this](AsyncWebServerRequest *r) {
            handleGetAppearance(r);
        });

        Http::onBody(server, "/api/appearance", HTTP_PATCH, Http::MAX_BODY_SMALL,
                     this, &AppearanceServer::handlePatchAppearance);
    }

    void AppearanceServer::handleGetAppearance(AsyncWebServerRequest *req)
    {
        char h0[8], h1[8], h2[8];

        jsonFormatHex(appearance.baseColor(0).r, appearance.baseColor(0).g, appearance.baseColor(0).b, h0);
        jsonFormatHex(appearance.baseColor(1).r, appearance.baseColor(1).g, appearance.baseColor(1).b, h1);
        jsonFormatHex(appearance.baseColor(2).r, appearance.baseColor(2).g, appearance.baseColor(2).b, h2);

        char buf[256];

        snprintf(buf, sizeof(buf),
                 "{\"brightness\":%u,\"baseColors\":[\"%s\",\"%s\",\"%s\"],\"palette\":\"%s\"}",
                 (unsigned)appearance.brightness(), h0, h1, h2, appearance.paletteName());
        Http::sendOkJson(req, buf);
    }

    void AppearanceServer::handlePatchAppearance(AsyncWebServerRequest *req, const uint8_t *body, size_t len)
    {
        // Validate everything synchronously (pure, no packets), collect the validated
        // values, then defer the actual apply — every AppearanceStore mutator broadcasts
        // to panels, which must happen on the main loop (see MainLoopQueue / PacketMirror).
        SimpleJson j(body, len);

        struct Args {
            AppearanceServer * self;
            bool               hasBrightness;
            uint8_t            brightness;
            bool               hasColors;
            Protocol::ColorRGB colors[BASE_COLORS_COUNT];
            bool               hasPalette;
            char               palette[20];
        } args{};

        args.self = this;

        if (j.hasKey("brightness")) {
            long v = j.getInt("brightness");

            if (v < 0 || v > 255) {
                Http::sendError(req, 422, "brightness_out_of_range");

                return;
            }

            args.hasBrightness = true;
            args.brightness    = (uint8_t)v;
        }

        const char *p = j.rawValue("baseColors");

        if (p && jsonEnterArray(p, j.end())) {
            uint8_t got = 0;
            char hex[16];

            while (got < BASE_COLORS_COUNT && jsonNextElement(p, j.end())) {
                if (!jsonReadString(p, j.end(), hex, sizeof(hex))) break;

                uint8_t r, g, b;

                if (jsonParseHexColor(hex, strlen(hex), &r, &g, &b)) args.colors[got++] = { r, g, b };
            }

            if (got == BASE_COLORS_COUNT) args.hasColors = true;
        }

        char palName[20];

        if (j.getString("palette", palName, sizeof(palName))) {
            if (!palettes.exists(palName)) {
                Http::sendError(req, 404, "unknown_palette");

                return;
            }

            args.hasPalette = true;
            strncpy(args.palette, palName, sizeof(args.palette) - 1);
        }

        bool queued = queue.post(+[](const uint8_t *a, uint16_t) {
            Args x;

            memcpy(&x, a, sizeof(x));

            if (x.hasBrightness) x.self->appearance.setBrightness(x.brightness);

            if (x.hasColors)     x.self->appearance.setAllBaseColors(x.colors);

            if (x.hasPalette)    x.self->appearance.setPalette(x.palette);

            x.self->animService.onAppearanceChanged(x.self->appearance.paletteName(),
                                                    x.self->appearance.baseColors());
        }, &args, sizeof(args));

        if (!queued) {
            Http::sendError(req, 503, "busy");

            return;
        }

        Http::sendAccepted(req);
    }
}  // namespace Lightnet
