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
        server.on("/api/brightness", HTTP_GET, [this](AsyncWebServerRequest *r) {
            handleGetBrightness(r);
        });
        server.on("/api/colors", HTTP_GET, [this](AsyncWebServerRequest *r) {
            handleGetColors(r);
        });
        server.on("/api/palette", HTTP_GET, [this](AsyncWebServerRequest *r) {
            handleGetPalette(r);
        });

        Http::onBody(server, "/api/appearance", HTTP_PUT, Http::MAX_BODY_SMALL,
                     this, &AppearanceServer::handlePutAppearance);
        Http::onBody(server, "/api/brightness", HTTP_PUT, Http::MAX_BODY_SMALL,
                     this, &AppearanceServer::handlePutBrightness);
        Http::onBody(server, "/api/colors", HTTP_PUT, Http::MAX_BODY_SMALL,
                     this, &AppearanceServer::handlePutColors);
        Http::onBody(server, "/api/palette", HTTP_PUT, Http::MAX_BODY_SMALL,
                     this, &AppearanceServer::handlePutPalette);
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

    void AppearanceServer::handleGetBrightness(AsyncWebServerRequest *req)
    {
        char buf[32];

        snprintf(buf, sizeof(buf), "{\"value\":%u}", (unsigned)appearance.brightness());
        Http::sendOkJson(req, buf);
    }

    void AppearanceServer::handleGetColors(AsyncWebServerRequest *req)
    {
        char h0[8], h1[8], h2[8];

        jsonFormatHex(appearance.baseColor(0).r, appearance.baseColor(0).g, appearance.baseColor(0).b, h0);
        jsonFormatHex(appearance.baseColor(1).r, appearance.baseColor(1).g, appearance.baseColor(1).b, h1);
        jsonFormatHex(appearance.baseColor(2).r, appearance.baseColor(2).g, appearance.baseColor(2).b, h2);

        char buf[128];

        snprintf(buf, sizeof(buf), "{\"primary\":\"%s\",\"secondary\":\"%s\",\"tertiary\":\"%s\"}", h0, h1, h2);
        Http::sendOkJson(req, buf);
    }

    void AppearanceServer::handleGetPalette(AsyncWebServerRequest *req)
    {
        char buf[64];

        snprintf(buf, sizeof(buf), "{\"palette\":\"%s\"}", appearance.paletteName());
        Http::sendOkJson(req, buf);
    }

    void AppearanceServer::handlePutBrightness(AsyncWebServerRequest *req, const uint8_t *body, size_t len)
    {
        SimpleJson j(body, len);
        long v = j.getInt("value");

        if (v < 0 || v > 255) {
            Http::sendError(req, 422, "value_out_of_range");

            return;
        }

        appearance.setBrightness((uint8_t)v);
        Http::sendOk(req);
    }

    void AppearanceServer::handlePutColors(AsyncWebServerRequest *req, const uint8_t *body, size_t len)
    {
        SimpleJson j(body, len);
        const char *slotNames[BASE_COLORS_COUNT] = { "primary", "secondary", "tertiary" };
        Protocol::ColorRGB newColors[BASE_COLORS_COUNT];
        bool touched[BASE_COLORS_COUNT] = { false, false, false };

        for (uint8_t i = 0; i < BASE_COLORS_COUNT; i++) {
            char hex[16];

            if (!j.getString(slotNames[i], hex, sizeof(hex))) continue;

            uint8_t r, g, b;

            if (!jsonParseHexColor(hex, strlen(hex), &r, &g, &b)) {
                Http::sendError(req, 422, "bad_hex_color");

                return;
            }

            newColors[i] = { r, g, b };
            touched[i] = true;
        }

        Protocol::ColorRGB all[BASE_COLORS_COUNT];
        bool any = false;

        for (uint8_t i = 0; i < BASE_COLORS_COUNT; i++) {
            all[i] = touched[i] ? newColors[i] : appearance.baseColor(i);

            if (touched[i]) any = true;
        }

        if (any) appearance.setAllBaseColors(all);

        Http::sendOk(req);
    }

    void AppearanceServer::handlePutPalette(AsyncWebServerRequest *req, const uint8_t *body, size_t len)
    {
        SimpleJson j(body, len);
        char name[20];

        if (!j.getString("palette", name, sizeof(name))) {
            Http::sendError(req, 422, "missing_palette");

            return;
        }

        if (!appearance.setPalette(name)) {
            Http::sendError(req, 404, "unknown_palette");

            return;
        }

        Http::sendOk(req);
    }

    void AppearanceServer::handlePutAppearance(AsyncWebServerRequest *req, const uint8_t *body, size_t len)
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
