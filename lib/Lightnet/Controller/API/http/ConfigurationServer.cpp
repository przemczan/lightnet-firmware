#include "ConfigurationServer.hpp"
#include "HttpHelpers.hpp"
#include "../../../Utils/SimpleJson.hpp"
#include <Arduino.h>
#include <string.h>
#include <stdio.h>

namespace Lightnet {
    ConfigurationServer::ConfigurationServer(
        AsyncWebServer&      _server,
        ConfigurationStore&  _config,
        TopologyConfigStore& _topology,
        ScenePlayer&         _player,
        MainLoopQueue&       _queue
    )
        : server(_server), config(_config), topology(_topology), player(_player), queue(_queue)
    {
    }

    void ConfigurationServer::begin()
    {
        registerRoutes();
    }

    void ConfigurationServer::registerRoutes()
    {
        Http::onRequest(server, "/api/configuration", HTTP_GET, this, &ConfigurationServer::handleGetConfiguration);
        Http::onBody(server, "/api/configuration", HTTP_PATCH, Http::MAX_BODY_LARGE,
                     this, &ConfigurationServer::handlePatchConfiguration);
    }

    namespace {
        // Heap state for the chunked GET /api/configuration response. A single panel
        // could in theory hold all of TopologyConfigStore's tag entries, so tags are
        // streamed one (panel,tag) entry at a time rather than via one large buffer —
        // owned solely via req->_tempObject + req->onDisconnect, freed exactly once.
        struct GetConfigurationState : Http::detail::RequestContext {
            ConfigurationStore * config;
            TopologyConfigStore *topology;
            size_t               entryIndex;
            bool                 emittedPrologue;
            bool                 inTagArray;
            uint8_t              currentPanel;
            bool                 firstPanel;
            bool                 entriesExhausted;
            bool                 emittedClose;
            char                 pending[48];
            size_t               pendingLen;
            size_t               pendingPos;
        };

        size_t configurationFill(GetConfigurationState *state, uint8_t *buf, size_t maxLen)
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

                if (!state->emittedPrologue) {
                    int n = snprintf(state->pending, sizeof(state->pending),
                                     "{\"powerStateOnBoot\":%u,\"logicalRoot\":%u,\"tags\":{",
                                     (unsigned)state->config->powerStateOnBoot(),
                                     (unsigned)state->topology->logicalRoot());

                    state->pendingLen      = (n > 0) ? (size_t)n : 0;
                    state->pendingPos      = 0;
                    state->emittedPrologue = true;

                    continue;
                }

                if (!state->entriesExhausted) {
                    uint8_t panel;
                    const char *tag;

                    if (!state->topology->tagEntryAt(state->entryIndex, panel, tag)) {
                        state->entriesExhausted = true;

                        continue;
                    }

                    int n;

                    if (!state->inTagArray || panel != state->currentPanel) {
                        n = snprintf(state->pending, sizeof(state->pending), "%s%s\"%u\":[\"%s\"",
                                     state->inTagArray ? "]" : "", state->firstPanel ? "" : ",",
                                     (unsigned)panel, tag);
                        state->inTagArray   = true;
                        state->currentPanel  = panel;
                        state->firstPanel    = false;
                    } else {
                        n = snprintf(state->pending, sizeof(state->pending), ",\"%s\"", tag);
                    }

                    state->pendingLen = (n > 0) ? (size_t)n : 0;
                    state->pendingPos = 0;
                    state->entryIndex++;

                    continue;
                }

                if (!state->emittedClose) {
                    snprintf(state->pending, sizeof(state->pending), "%s}}", state->inTagArray ? "]" : "");
                    state->pendingLen   = strlen(state->pending);
                    state->pendingPos   = 0;
                    state->emittedClose = true;

                    continue;
                }

                break;
            }

            return written;
        }
    } // namespace

    void ConfigurationServer::handleGetConfiguration(AsyncWebServerRequest *req)
    {
        auto *state = (GetConfigurationState *)malloc(sizeof(GetConfigurationState));

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

        state->config           = &config;
        state->topology         = &topology;
        state->entryIndex       = 0;
        state->emittedPrologue  = false;
        state->inTagArray       = false;
        state->currentPanel     = 0;
        state->firstPanel       = true;
        state->entriesExhausted = false;
        state->emittedClose     = false;
        state->pendingLen       = 0;
        state->pendingPos       = 0;

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
            size_t written = configurationFill(state, buf, maxLen);

            Http::logFillTick(req, written, maxLen);

            if (written == 0) Http::logChunkedComplete(req);

            return written;
        });

        Http::sendOkChunked(req, res);
    }

    void ConfigurationServer::handlePatchConfiguration(AsyncWebServerRequest *req, const uint8_t *body, size_t len)
    {
        const char *p   = (const char *)body;
        const char *end = p + len;

        if (!jsonEnterObject(p, end)) {
            Http::sendError(req, 400, "expected_object");

            return;
        }

        bool foundPower = false;
        bool foundRoot  = false;
        bool foundTags  = false;
        long root       = -1;
        char key[20];
        char tagErr[64] = { 0 };

        while (jsonNextKey(p, end, key, sizeof(key))) {
            if (strcmp(key, "powerStateOnBoot") == 0) {
                long v;

                if (!jsonReadUInt(p, end, &v)) {
                    Http::sendError(req, 400, "powerStateOnBoot: expected integer");

                    return;
                }

                if (!config.setPowerStateOnBoot((uint8_t)v)) {
                    Http::sendError(req, 422, "powerStateOnBoot: must be 0, 1, or 2");

                    return;
                }

                foundPower = true;
            } else if (strcmp(key, "logicalRoot") == 0) {
                if (!jsonReadUInt(p, end, &root)) {
                    Http::sendError(req, 400, "logicalRoot: not a uint");

                    return;
                }

                if (root < 0 || root > 255) {
                    Http::sendError(req, 422, "logicalRoot: must be 0-255");

                    return;
                }

                foundRoot = true;
            } else if (strcmp(key, "tags") == 0) {
                if (!topology.replaceTagsAt(p, end, tagErr, sizeof(tagErr))) {
                    Http::sendError(req, 422, tagErr[0] ? tagErr : "tags: invalid");

                    return;
                }

                foundTags = true;
            } else {
                jsonSkipValue(p, end);
            }
        }

        if (!foundPower && !foundRoot && !foundTags) {
            Http::sendError(req, 400, "no recognized fields");

            return;
        }

        if (foundRoot) {
            struct Args {
                ConfigurationServer *self;
                uint8_t              root;
            } args { this, (uint8_t)root };

            bool queued = queue.post(+[](const uint8_t *a, uint16_t) {
                Args x;

                memcpy(&x, a, sizeof(x));
                x.self->topology.setLogicalRoot(x.root);
                x.self->player.setLogicalRoot(x.root, millis());
            }, &args, sizeof(args));

            if (!queued) {
                Http::sendError(req, 503, "busy");

                return;
            }

            char buf[48];

            snprintf(buf, sizeof(buf), "{\"ok\":true,\"logicalRoot\":%u}", (unsigned)root);
            Http::sendAcceptedJson(req, buf);

            return;
        }

        Http::sendOk(req);
    }
}  // namespace Lightnet
