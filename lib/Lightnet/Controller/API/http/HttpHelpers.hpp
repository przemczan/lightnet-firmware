#pragma once
// Shared helpers for the HTTP server classes (PaletteServer, SceneServer,
// AppearanceServer, AnimationServer, PanelServer).
//
// What lives here:
//   - Response helpers (sendOk / sendOkJson / sendOkStream / sendNoContent / sendError)
//   - Body-buffering route registration (onBody) — accumulates a
//     potentially-chunked request body into a single buffer and dispatches to
//     a member function once complete. The buffer is always null-terminated.
//   - No-body route registration (onRequest) — sets up timing context and
//     dispatches to a member function.
//   - URL/name parsing (isSafeName, nameFromUrl)
//   - Body-size constants (MAX_BODY_SMALL, MAX_BODY_LARGE)

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
        constexpr size_t MAX_BODY_LARGE = 1024 * 6;

        // ============================================================================
        // Body buffering (chunked request body → single null-terminated buffer)
        // ============================================================================

        namespace detail {
            constexpr size_t LOG_TRUNCATE = 80;

            // Stored in req->_tempObject for every routed request.
            // BodyBuf inherits this so logResponse can read startMs uniformly.
            struct RequestContext {
                uint32_t startMs;
            };

            inline uint32_t elapsedMs(AsyncWebServerRequest *req)
            {
                if (!req->_tempObject) return 0;

                return millis() - static_cast<RequestContext *>(req->_tempObject)->startMs;
            }

            inline void logResponse(AsyncWebServerRequest *req, int status, const char *body)
            {
                uint32_t ms   = elapsedMs(req);
                size_t blen = body ? strlen(body) : 0;

                if (blen > LOG_TRUNCATE)
                    D_PRINTF("[HTTP] %s %s -> %d (%ums) %.*s...\n",
                             req->methodToString(), req->url().c_str(), status,
                             (unsigned)ms, (int)LOG_TRUNCATE, body);
                else
                    D_PRINTF("[HTTP] %s %s -> %d (%ums) %s\n",
                             req->methodToString(), req->url().c_str(), status,
                             (unsigned)ms, body ? body : "");
            }

            inline void logBody(AsyncWebServerRequest *req, const uint8_t *body, size_t len)
            {
                size_t show = (len < LOG_TRUNCATE) ? len : LOG_TRUNCATE;

                D_PRINTF("[HTTP] %s %s <- %.*s%s\n",
                         req->methodToString(), req->url().c_str(), (int)show, (const char *)body,
                         len > LOG_TRUNCATE ? "..." : "");
            }

            struct BodyBuf : RequestContext {
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

                    buf = (BodyBuf *)malloc(sizeof(BodyBuf) + cap + 1);

                    if (!buf) return false;

                    buf->startMs = millis();
                    buf->len     = 0;
                    buf->cap     = cap;
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

        // For streaming responses built with req->beginResponseStream().
        // Logs the response before handing the stream off to AsyncWebServer.
        inline void sendOkStream(AsyncWebServerRequest *req, AsyncResponseStream *res)
        {
            DEBUG_IF(DEBUG_API, {
                uint32_t ms = detail::elapsedMs(req);
                D_PRINTF("[HTTP] %s %s -> 200 (%ums) [stream]\n",
                         req->methodToString(), req->url().c_str(), (unsigned)ms);
            });
            req->send(res);
        }

        // 202 Accepted: the request validated and its work was queued onto the main
        // loop (see MainLoopQueue). Used by mutating endpoints whose side effects —
        // I2C packet emission, ScenePlayer changes — must not run on the AsyncTCP task.
        inline void sendAccepted(AsyncWebServerRequest *req)
        {
            DEBUG_IF(DEBUG_API, detail::logResponse(req, 202, "{\"ok\":true}"));
            req->send(202, "application/json", "{\"ok\":true}");
        }

        // 202 with a caller-supplied JSON body (e.g. to echo the accepted value).
        inline void sendAcceptedJson(AsyncWebServerRequest *req, const char *json)
        {
            DEBUG_IF(DEBUG_API, detail::logResponse(req, 202, json));
            req->send(202, "application/json", json);
        }

        inline void sendError(AsyncWebServerRequest *req, int code, const char *msg)
        {
            char buf[128];

            snprintf(buf, sizeof(buf), "{\"error\":\"%s\"}", msg ? msg : "error");
            DEBUG_IF(DEBUG_API, detail::logResponse(req, code, buf));
            req->send(code, "application/json", buf);
        }

        inline void sendNoContent(AsyncWebServerRequest *req)
        {
            DEBUG_IF(DEBUG_API, {
                uint32_t ms = detail::elapsedMs(req);
                D_PRINTF("[HTTP] %s %s -> 204 (%ums)\n",
                         req->methodToString(), req->url().c_str(), (unsigned)ms);
            });
            req->send(204);
        }

        // ============================================================================
        // Route registration helpers
        // ============================================================================

        // Register a no-body route (GET, DELETE, no-body POST, etc.) with timing.
        // The timing context is stored in req->_tempObject so logResponse can report
        // request duration. Signature: void T::method(AsyncWebServerRequest *).
        template <typename T>
        void onRequest(
            AsyncWebServer&  server,
            const char *     uri,
            WebRequestMethod method,
            T *              instance,
            void (T::*       memberFn)(AsyncWebServerRequest *)
        )
        {
            server.on(uri, method, [instance, memberFn](AsyncWebServerRequest *req) {
                auto *ctx = static_cast<detail::RequestContext *>(malloc(sizeof(detail::RequestContext)));

                if (ctx) {
                    ctx->startMs = millis();
                    req->_tempObject = ctx;
                    req->onDisconnect([req]() {
                        free(req->_tempObject);
                        req->_tempObject = nullptr;
                    });
                }

                (instance->*memberFn)(req);
            });
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
