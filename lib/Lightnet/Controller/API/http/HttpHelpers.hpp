#pragma once
// Shared helpers for the HTTP server classes (PaletteServer, SceneServer,
// AppearanceServer, AnimationServer, PanelServer).
//
// What lives here:
//   - Response helpers (httpSendOk / httpSendOkJson / httpSendError)
//   - Body-buffering route registration (httpOnBody) — accumulates a
//     potentially-chunked request body into a single buffer and dispatches to
//     a member function once complete. The buffer is always null-terminated.
//   - URL/name parsing (httpIsSafeName, httpNameFromUrl)
//   - Body-size constants (MAX_BODY_SMALL, MAX_BODY_LARGE)
//
// Why it exists:
//   Each server used to duplicate ~50 lines of body-buffering glue. The
//   duplicated copies all shared the same bug (no null terminator after the
//   accumulated body), and any new server would have inherited it. Centralising
//   the implementation here means handlers only deal with their actual data.

#include <ESPAsyncWebServer.h>
#include "HttpUrl.hpp"
#include "../../../Utils/Debug.hpp"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

namespace Lightnet {
    namespace Http {
        constexpr size_t MAX_BODY_SMALL = 512;
        constexpr size_t MAX_BODY_LARGE = 4096;

        // ============================================================================
        // Body buffering (chunked request body → single null-terminated buffer)
        // ============================================================================

        namespace detail {
            constexpr size_t LOG_TRUNCATE = 80;

            inline void logResponse(AsyncWebServerRequest *req, int status, const char *body)
            {
                size_t blen = body ? strlen(body) : 0;

                if (blen > LOG_TRUNCATE)
                    D_PRINTF("[HTTP] %s %s -> %d %.*s...\n",
                             req->methodToString(), req->url().c_str(), status, (int)LOG_TRUNCATE, body);
                else
                    D_PRINTF("[HTTP] %s %s -> %d %s\n",
                             req->methodToString(), req->url().c_str(), status, body ? body : "");
            }

            inline void logBody(AsyncWebServerRequest *req, const uint8_t *body, size_t len)
            {
                size_t show = len < LOG_TRUNCATE ? len : LOG_TRUNCATE;

                D_PRINTF("[HTTP] %s %s <- %.*s%s\n",
                         req->methodToString(), req->url().c_str(), (int)show, (const char *)body,
                         len > LOG_TRUNCATE ? "..." : "");
            }

            struct BodyBuf {
                size_t  len, cap;
                uint8_t data[1];
            };

            inline bool appendBody(
                AsyncWebServerRequest *req,
                const uint8_t *        data,
                size_t                 len,
                size_t                 total,
                size_t                 maxCap
            )
            {
                BodyBuf *buf = (BodyBuf *)req->_tempObject;

                if (!buf) {
                    size_t cap = (total > 0) ? total : 256;

                    if (cap > maxCap) cap = maxCap;

                    // +1 byte for the trailing null terminator.
                    buf = (BodyBuf *)malloc(sizeof(BodyBuf) + cap + 1);

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
                buf->data[buf->len] = '\0';

                return true;
            }
        } // namespace detail

        // ============================================================================
        // Response helpers
        // ============================================================================

        // URL / name parsing helpers (isSafeName, nameFromUrl) live in HttpUrl.hpp
        // so they can be unit-tested without ESPAsyncWebServer.

        inline void sendOk(AsyncWebServerRequest *req)
        {
            DEBUG_IF(DEBUG_API, detail::logResponse(req, 200, "{\"ok\":true}"));
            req->send(200, "application/json", "{\"ok\":true}");
        }

        inline void sendOkJson(AsyncWebServerRequest *req, const char *json)
        {
            DEBUG_IF(DEBUG_API, detail::logResponse(req, 200, json));
            req->send(200, "application/json", json);
        }

        inline void sendError(AsyncWebServerRequest *req, int code, const char *msg)
        {
            char buf[128];

            snprintf(buf, sizeof(buf), "{\"error\":\"%s\"}", msg ? msg : "error");
            DEBUG_IF(DEBUG_API, detail::logResponse(req, code, buf));
            req->send(code, "application/json", buf);
        }

        // Register a route that buffers the request body, then dispatches to a member
        // function with signature: void T::method(AsyncWebServerRequest *, const uint8_t *body, size_t len).
        //
        // Usage:
        //   Http::onBody(server, "/api/palettes", HTTP_POST, Http::MAX_BODY_LARGE,
        //                this, &PaletteServer::handlePostPalette);
        template <typename T>
        void onBody(
            AsyncWebServer& server,
            const char *uri,
            WebRequestMethod method,
            size_t maxBody,
            T *instance,
            void (T::* memberFn)(AsyncWebServerRequest *, const uint8_t *, size_t)
        )
        {
            auto onRequest = [instance, memberFn](AsyncWebServerRequest *req) {
                                 detail::BodyBuf *buf = (detail::BodyBuf *)req->_tempObject;

                                 if (!buf) {
                                     sendError(req, 400, "empty_body");

                                     return;
                                 }

                                 DEBUG_IF(DEBUG_API, detail::logBody(req, buf->data, buf->len));
                                 (instance->*memberFn)(req, buf->data, buf->len);
                             };
            auto onChunk = [maxBody](
                AsyncWebServerRequest *r,
                uint8_t *d,
                size_t l,
                size_t /*index*/,
                size_t t
                           ) {
                               if (!detail::appendBody(r, d, l, t, maxBody)) {
                                   sendError(r, 413, "body_too_large");
                               }
                           };

            server.on(uri, method, onRequest, nullptr, onChunk);
        }
    } // namespace Http
}  // namespace Lightnet
