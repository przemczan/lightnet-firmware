#include "AppearanceServer.hpp"
#include "HttpHelpers.hpp"
#include "../../../Utils/SimpleJson.hpp"
#include <Arduino.h>
#include <string.h>
#include <stdio.h>

namespace Lightnet {
    AppearanceServer::AppearanceServer(
        AsyncWebServer&  _server,
        AppearanceStore& _appearance,
        PaletteStore&    _palettes
    )
        : server(_server), appearance(_appearance), palettes(_palettes)
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
        SimpleJson j(body, len);

        if (j.hasKey("brightness")) {
            long v = j.getInt("brightness");

            if (v < 0 || v > 255) {
                Http::sendError(req, 422, "brightness_out_of_range");

                return;
            }

            appearance.setBrightness((uint8_t)v);
        }

        const char *p = j.rawValue("baseColors");

        if (p && jsonEnterArray(p, j.end())) {
            Protocol::ColorRGB cur[BASE_COLORS_COUNT];
            uint8_t got = 0;
            char hex[16];

            while (got < BASE_COLORS_COUNT && jsonNextElement(p, j.end())) {
                if (!jsonReadString(p, j.end(), hex, sizeof(hex))) break;

                uint8_t r, g, b;

                if (jsonParseHexColor(hex, strlen(hex), &r, &g, &b)) cur[got++] = { r, g, b };
            }

            if (got == BASE_COLORS_COUNT) appearance.setAllBaseColors(cur);
        }

        char palName[20];

        if (j.getString("palette", palName, sizeof(palName))) {
            if (!appearance.setPalette(palName)) {
                Http::sendError(req, 404, "unknown_palette");

                return;
            }
        }

        Http::sendOk(req);
    }
}  // namespace Lightnet
