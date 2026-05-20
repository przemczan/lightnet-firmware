#include "AnimationServer.hpp"
#include "../Utils/Debug.hpp"
#include <Arduino.h>
#include <string.h>
#include <stdlib.h>

namespace Lightnet {

namespace {

// ----- Tiny ad-hoc JSON parsers (small known-shape bodies only) -----

void skipWs(const char*& p, const char* end) {
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
}

// Find a top-level key like "brightness" inside a `{...}` object. Returns a pointer to
// the value start (after the colon and whitespace), or nullptr if not found.
const char* findKey(const char* body, size_t len, const char* key) {
    const char* end = body + len;
    size_t klen = strlen(key);
    for (const char* p = body; p + klen + 2 <= end; p++) {
        if (*p != '"') continue;
        if (strncmp(p + 1, key, klen) != 0) continue;
        if (p[1 + klen] != '"') continue;
        const char* q = p + klen + 2;
        skipWs(q, end);
        if (q >= end || *q != ':') continue;
        q++;
        skipWs(q, end);
        return q;
    }
    return nullptr;
}

// Parse a non-negative integer starting at `p` (already past whitespace). Returns -1 on
// non-digit. Stops at first non-digit.
long parseUInt(const char* p, const char* end) {
    if (p >= end || *p < '0' || *p > '9') return -1;
    long v = 0;
    while (p < end && *p >= '0' && *p <= '9') {
        v = v * 10 + (*p - '0');
        p++;
        if (v > 100000) return -1;  // sanity
    }
    return v;
}

// Parse a JSON string into `out` (max outLen bytes incl. null). Returns true on success.
bool parseString(const char* p, const char* end, char* out, size_t outLen) {
    if (p >= end || *p != '"') return false;
    p++;
    size_t i = 0;
    while (p < end && *p != '"' && i + 1 < outLen) {
        out[i++] = *p++;
    }
    if (p >= end || *p != '"') return false;
    out[i] = '\0';
    return true;
}

bool parseHexColor(const char* s, size_t len, Protocol::ColorRGB* out) {
    if (len != 7 || s[0] != '#') return false;
    auto h = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    int r1 = h(s[1]), r0 = h(s[2]);
    int g1 = h(s[3]), g0 = h(s[4]);
    int b1 = h(s[5]), b0 = h(s[6]);
    if (r1 < 0 || r0 < 0 || g1 < 0 || g0 < 0 || b1 < 0 || b0 < 0) return false;
    out->r = (uint8_t)((r1 << 4) | r0);
    out->g = (uint8_t)((g1 << 4) | g0);
    out->b = (uint8_t)((b1 << 4) | b0);
    return true;
}

void formatHex(Protocol::ColorRGB c, char* out) {
    snprintf(out, 8, "#%02X%02X%02X", c.r, c.g, c.b);
}

// Build a per-request body buffer attached as the request's _tempObject pointer.
struct BodyBuf {
    size_t len;
    size_t cap;
    uint8_t data[1];  // flexible
};

BodyBuf* getOrCreateBuf(AsyncWebServerRequest* req, size_t total) {
    BodyBuf* buf = (BodyBuf*)req->_tempObject;
    if (buf) return buf;
    size_t cap = total > 0 ? total : 256;
    if (cap > 512) cap = 512;
    buf = (BodyBuf*)malloc(sizeof(BodyBuf) + cap);
    if (!buf) return nullptr;
    buf->len = 0;
    buf->cap = cap;
    req->_tempObject = buf;
    req->onDisconnect([req]() {
        if (req->_tempObject) {
            free(req->_tempObject);
            req->_tempObject = nullptr;
        }
    });
    return buf;
}

bool appendBody(AsyncWebServerRequest* req, const uint8_t* data, size_t len, size_t total) {
    BodyBuf* buf = getOrCreateBuf(req, total);
    if (!buf) return false;
    if (buf->len + len > buf->cap) return false;  // too big
    memcpy(buf->data + buf->len, data, len);
    buf->len += len;
    return true;
}

}  // anonymous namespace

AnimationServer::AnimationServer(AsyncWebServer& _server, AppearanceStore& _appearance, PaletteStore& _palettes)
    : server(_server), appearance(_appearance), palettes(_palettes) {}

void AnimationServer::begin()
{
    registerRoutes();
}

void AnimationServer::registerRoutes()
{
    // ----- GETs -----

    server.on("/api/appearance", HTTP_GET, [this](AsyncWebServerRequest* req) {
        handleGetAppearance(req);
    });
    server.on("/api/brightness", HTTP_GET, [this](AsyncWebServerRequest* req) {
        handleGetBrightness(req);
    });
    server.on("/api/colors", HTTP_GET, [this](AsyncWebServerRequest* req) {
        handleGetColors(req);
    });
    server.on("/api/palette", HTTP_GET, [this](AsyncWebServerRequest* req) {
        handleGetPalette(req);
    });
    server.on("/api/palettes", HTTP_GET, [this](AsyncWebServerRequest* req) {
        handleListPalettes(req);
    });

    // ----- PUTs (body-driven) -----
    // AsyncWebServer's onBody fires per chunk; the request handler fires after the
    // last chunk. We accumulate body into a temp buffer and parse in the handler.

    auto putHandler = [this](void (AnimationServer::*method)(AsyncWebServerRequest*, const uint8_t*, size_t)) {
        return [this, method](AsyncWebServerRequest* req) {
            BodyBuf* buf = (BodyBuf*)req->_tempObject;
            if (!buf) {
                req->send(400, "application/json", "{\"error\":\"empty_body\"}");
                return;
            }
            (this->*method)(req, buf->data, buf->len);
        };
    };

    auto bodyAccumulator = [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
        if (!appendBody(req, data, len, total)) {
            req->send(413, "application/json", "{\"error\":\"body_too_large\"}");
        }
    };

    server.on("/api/brightness", HTTP_PUT, putHandler(&AnimationServer::handlePutBrightness), nullptr, bodyAccumulator);
    server.on("/api/colors",     HTTP_PUT, putHandler(&AnimationServer::handlePutColors),     nullptr, bodyAccumulator);
    server.on("/api/palette",    HTTP_PUT, putHandler(&AnimationServer::handlePutPalette),    nullptr, bodyAccumulator);
    server.on("/api/appearance", HTTP_PUT, putHandler(&AnimationServer::handlePutAppearance), nullptr, bodyAccumulator);
}

// ============================================================================
// GETs
// ============================================================================

void AnimationServer::handleGetAppearance(AsyncWebServerRequest* req)
{
    char hex0[8], hex1[8], hex2[8];
    formatHex(appearance.baseColor(0), hex0);
    formatHex(appearance.baseColor(1), hex1);
    formatHex(appearance.baseColor(2), hex2);

    char buf[256];
    snprintf(buf, sizeof(buf),
        "{\"brightness\":%u,"
        "\"baseColors\":[\"%s\",\"%s\",\"%s\"],"
        "\"palette\":\"%s\"}",
        (unsigned)appearance.brightness(), hex0, hex1, hex2, appearance.paletteName());
    req->send(200, "application/json", buf);
}

void AnimationServer::handleGetBrightness(AsyncWebServerRequest* req)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "{\"value\":%u}", (unsigned)appearance.brightness());
    req->send(200, "application/json", buf);
}

void AnimationServer::handleGetColors(AsyncWebServerRequest* req)
{
    char h0[8], h1[8], h2[8];
    formatHex(appearance.baseColor(0), h0);
    formatHex(appearance.baseColor(1), h1);
    formatHex(appearance.baseColor(2), h2);
    char buf[128];
    snprintf(buf, sizeof(buf), "{\"primary\":\"%s\",\"secondary\":\"%s\",\"tertiary\":\"%s\"}", h0, h1, h2);
    req->send(200, "application/json", buf);
}

void AnimationServer::handleGetPalette(AsyncWebServerRequest* req)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"palette\":\"%s\"}", appearance.paletteName());
    req->send(200, "application/json", buf);
}

void AnimationServer::handleListPalettes(AsyncWebServerRequest* req)
{
    char buf[256];
    int n = snprintf(buf, sizeof(buf), "[");
    bool first = true;
    n += snprintf(buf + n, sizeof(buf) - n, "%s\"userColors\"", first ? "" : ",");
    first = false;
    for (uint8_t i = 0; i < palettes.builtInCount() && n + 32 < (int)sizeof(buf); i++) {
        n += snprintf(buf + n, sizeof(buf) - n, ",\"%s\"", palettes.builtInName(i));
    }
    snprintf(buf + n, sizeof(buf) - n, "]");
    req->send(200, "application/json", buf);
}

// ============================================================================
// PUTs
// ============================================================================

void AnimationServer::handlePutBrightness(AsyncWebServerRequest* req, const uint8_t* body, size_t len)
{
    const char* v = findKey((const char*)body, len, "value");
    if (!v) { req->send(422, "application/json", "{\"error\":\"missing_value\"}"); return; }
    long n = parseUInt(v, (const char*)body + len);
    if (n < 0 || n > 255) { req->send(422, "application/json", "{\"error\":\"value_out_of_range\"}"); return; }

    appearance.setBrightness((uint8_t)n);
    req->send(200, "application/json", "{\"ok\":true}");
}

void AnimationServer::handlePutColors(AsyncWebServerRequest* req, const uint8_t* body, size_t len)
{
    const char* slotNames[BASE_COLORS_COUNT] = { "primary", "secondary", "tertiary" };
    Protocol::ColorRGB colors[BASE_COLORS_COUNT];
    bool touched[BASE_COLORS_COUNT] = { false, false, false };

    const char* end = (const char*)body + len;

    for (uint8_t i = 0; i < BASE_COLORS_COUNT; i++) {
        const char* v = findKey((const char*)body, len, slotNames[i]);
        if (!v) continue;
        char hex[16];
        if (!parseString(v, end, hex, sizeof(hex))) {
            req->send(422, "application/json", "{\"error\":\"color_not_string\"}");
            return;
        }
        Protocol::ColorRGB c;
        if (!parseHexColor(hex, strlen(hex), &c)) {
            req->send(422, "application/json", "{\"error\":\"bad_hex_color\"}");
            return;
        }
        colors[i] = c;
        touched[i] = true;
    }

    // Apply only touched slots
    for (uint8_t i = 0; i < BASE_COLORS_COUNT; i++) {
        if (touched[i]) {
            appearance.setBaseColor(i, colors[i]);
        }
    }
    req->send(200, "application/json", "{\"ok\":true}");
}

void AnimationServer::handlePutPalette(AsyncWebServerRequest* req, const uint8_t* body, size_t len)
{
    const char* v = findKey((const char*)body, len, "palette");
    if (!v) { req->send(422, "application/json", "{\"error\":\"missing_palette\"}"); return; }
    char name[20];
    if (!parseString(v, (const char*)body + len, name, sizeof(name))) {
        req->send(422, "application/json", "{\"error\":\"palette_not_string\"}");
        return;
    }
    if (!appearance.setPalette(name)) {
        req->send(404, "application/json", "{\"error\":\"unknown_palette\"}");
        return;
    }
    req->send(200, "application/json", "{\"ok\":true}");
}

void AnimationServer::handlePutAppearance(AsyncWebServerRequest* req, const uint8_t* body, size_t len)
{
    // Bulk update: any subset of {brightness, baseColors:[..,..,..]/primary/secondary/tertiary, palette}.
    const char* end = (const char*)body + len;

    // Brightness
    const char* v = findKey((const char*)body, len, "brightness");
    if (v) {
        long n = parseUInt(v, end);
        if (n < 0 || n > 255) { req->send(422, "application/json", "{\"error\":\"brightness_out_of_range\"}"); return; }
        appearance.setBrightness((uint8_t)n);
    }

    // baseColors array form
    const char* bc = findKey((const char*)body, len, "baseColors");
    if (bc) {
        const char* p = bc;
        skipWs(p, end);
        if (p < end && *p == '[') {
            p++;
            Protocol::ColorRGB cur[BASE_COLORS_COUNT];
            uint8_t got = 0;
            while (p < end && got < BASE_COLORS_COUNT) {
                skipWs(p, end);
                if (*p == ']') break;
                char hex[16];
                if (!parseString(p, end, hex, sizeof(hex))) break;
                Protocol::ColorRGB c;
                if (parseHexColor(hex, strlen(hex), &c)) {
                    cur[got++] = c;
                }
                while (p < end && *p != '"' && *p != ',' && *p != ']') p++;  // skip past consumed string
                if (p < end && *p == '"') p++;
                skipWs(p, end);
                if (p < end && *p == ',') p++;
            }
            if (got == BASE_COLORS_COUNT) {
                appearance.setAllBaseColors(cur);
            }
        }
    }

    // Palette
    v = findKey((const char*)body, len, "palette");
    if (v) {
        char name[20];
        if (!parseString(v, end, name, sizeof(name))) {
            req->send(422, "application/json", "{\"error\":\"palette_not_string\"}");
            return;
        }
        if (!appearance.setPalette(name)) {
            req->send(404, "application/json", "{\"error\":\"unknown_palette\"}");
            return;
        }
    }

    req->send(200, "application/json", "{\"ok\":true}");
}

}  // namespace Lightnet
