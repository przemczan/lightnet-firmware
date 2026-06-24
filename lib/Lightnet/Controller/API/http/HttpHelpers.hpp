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
#include "../../../Utils/SimpleJson.hpp"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <functional>

namespace Lightnet {
    namespace Http {
        constexpr size_t MAX_BODY_SMALL = 512;
        constexpr size_t MAX_BODY_LARGE = 1024 * 5;

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
                    D_PRINTFLN("[HTTP] %s %s -> %d (%ums) %.*s...",
                               req->methodToString(), req->url().c_str(), status,
                               (unsigned)ms, (int)LOG_TRUNCATE, body);
                else
                    D_PRINTFLN("[HTTP] %s %s -> %d (%ums) %s",
                               req->methodToString(), req->url().c_str(), status,
                               (unsigned)ms, body ? body : "");
            }

            inline void logBody(AsyncWebServerRequest *req, const uint8_t *body, size_t len)
            {
                size_t show = (len < LOG_TRUNCATE) ? len : LOG_TRUNCATE;

                D_PRINTFLN("[HTTP] %s %s <- %.*s%s",
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
                D_PRINTFLN("[HTTP] %s %s -> 200 (%ums) [stream]",
                           req->methodToString(), req->url().c_str(), (unsigned)ms);
            });
            req->send(res);
        }

        // For chunked responses built with req->beginChunkedResponse(). Does NOT log —
        // unlike sendOkStream, the content isn't fully generated yet at this point (the
        // fill callback streams it incrementally as TCP buffer space frees up), so timing
        // here would only measure setup, not the real request duration. Call
        // logChunkedComplete(req) from the fill callback once it returns 0 (the signal
        // ESPAsyncWebServer uses to end the chunked stream — see AsyncAbstractResponse::_ack).
        inline void sendOkChunked(AsyncWebServerRequest *req, AsyncWebServerResponse *res)
        {
            req->send(res);
        }

        // Call from a beginChunkedResponse fill callback exactly when it returns 0
        // (stream finished generating content) to log the true end-to-end duration.
        inline void logChunkedComplete(AsyncWebServerRequest *req)
        {
            DEBUG_IF(DEBUG_API, {
                uint32_t ms = detail::elapsedMs(req);
                D_PRINTFLN("[HTTP] %s %s -> 200 (%ums) [chunked]",
                           req->methodToString(), req->url().c_str(), (unsigned)ms);
            });
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

            if (jsonWriteErrorObject(buf, sizeof(buf), msg ? msg : "error") < 0) {
                strcpy(buf, "{\"error\":\"error\"}");
            }

            DEBUG_IF(DEBUG_API, detail::logResponse(req, code, buf));
            req->send(code, "application/json", buf);
        }

        inline void sendNoContent(AsyncWebServerRequest *req)
        {
            DEBUG_IF(DEBUG_API, {
                uint32_t ms = detail::elapsedMs(req);
                D_PRINTFLN("[HTTP] %s %s -> 204 (%ums)",
                           req->methodToString(), req->url().c_str(), (unsigned)ms);
            });
            req->send(204);
        }

        // ============================================================================
        // Debug-only connection lifecycle instrumentation
        //
        // logChunkedComplete() above only proves the firmware finished GENERATING
        // content (the fill callback returned 0) — it says nothing about whether
        // those bytes ever reached the client. Don't hook AsyncClient's onAck/
        // onTimeout/onError directly (req->client()->onAck(...) etc.): AsyncWebServerRequest's
        // constructor already wires those to its own internal handlers that drive the
        // chunked-response state machine and the framework's stall-recovery — replacing
        // them silently breaks response delivery. req->onDisconnect(...) is the only
        // safe app-owned hook, and it fires whether the connection closes normally
        // after full delivery or is force-closed by the framework's own ack-timeout.
        // ============================================================================

        // Wraps a route's req->onDisconnect(...) registration to additionally log the
        // true end-to-end connection lifetime (elapsed time + heap stats) at the
        // moment the socket actually closes, then runs the route's own cleanup.
        // Compare against logChunkedComplete()'s timestamp to see how much time was
        // spent flushing after content generation was already "done".
        inline void onDisconnectLogged(AsyncWebServerRequest *req, std::function<void()> cleanup = nullptr)
        {
            req->onDisconnect([req, cleanup]() {
                DEBUG_IF(DEBUG_API, {
                    uint32_t ms = detail::elapsedMs(req);
                    D_PRINTF("[HTTP][CLOSE] %s %s after %ums (heap free=%u",
                             req->methodToString(), req->url().c_str(), (unsigned)ms,
                             (unsigned)ESP.getFreeHeap());
                    #ifdef ARDUINO_ARCH_ESP8266
                        D_PRINTF(" frag%%=%u maxBlock=%u", (unsigned)ESP.getHeapFragmentation(),
                                 (unsigned)ESP.getMaxFreeBlockSize());
                    #endif
                    D_PRINTF(")\n");
                });

                if (cleanup) cleanup();
            });
        }

        // Call from inside a beginChunkedResponse fill lambda on EVERY invocation
        // (not just when written == 0) to see the actual send cadence. A long gap
        // between consecutive ticks points at ack-stalls/heap-pressure retries inside
        // the framework's _ack(), as opposed to the handler itself being slow to
        // build content.
        inline void logFillTick(AsyncWebServerRequest *req, size_t written, size_t maxLen)
        {
            DEBUG_IF(DEBUG_API, {
                uint32_t ms = detail::elapsedMs(req);
                D_PRINTFLN("[HTTP][FILL] %s %s wrote=%u/%u t=%ums heap=%u",
                           req->methodToString(), req->url().c_str(),
                           (unsigned)written, (unsigned)maxLen, (unsigned)ms,
                           (unsigned)ESP.getFreeHeap());
            });
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
