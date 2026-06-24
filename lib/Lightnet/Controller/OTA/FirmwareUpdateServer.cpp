#include "FirmwareUpdateServer.hpp"

#ifdef LIGHTNET_TARGET_CONTROLLER

    #include "../../Utils/Debug.hpp"
    #include "../../Utils/SimpleJson.hpp"

    static const char *FIRMWARE_PATH = "/panel_fw.bin";

    FirmwareUpdateServer::FirmwareUpdateServer(AsyncWebServer *server, PanelFlasher *flasher)
        : flasher(flasher)
    {
        // POST /api/firmware/panels — upload + flash
        server->on(
            "/api/firmware/panels",
            HTTP_POST,
            // Request-complete handler: file is fully written, trigger flashing
            [this](AsyncWebServerRequest *request) {
        if (this->flasher->isActive()) {
            request->send(409, "application/json",
                          "{\"error\":\"flash already in progress\"}");

            return;
        }

        D_PRINTLN("[FW] upload complete, starting panel flash");
        this->flasher->startFlashing(FIRMWARE_PATH);

        PanelFlasher::Status s = this->flasher->getStatus();

        if (s.hasError) {
            char body[128];

            if (Lightnet::jsonWriteErrorObject(body, sizeof(body), s.errorMsg) < 0) {
                strcpy(body, "{\"error\":\"flash error\"}");
            }

            request->send(422, "application/json", body);
        } else {
            char body[64];
            snprintf(body, sizeof(body),
                     "{\"status\":\"flashing\",\"panels\":%d}", s.totalPanels);
            request->send(200, "application/json", body);
        }
    },
            nullptr, // file upload handler (unused — we handle raw body below)
            // Body chunk handler: stream directly to filesystem
            [this](AsyncWebServerRequest *request, uint8_t *data,
                   size_t len, size_t index, size_t total) {
        this->handleUploadBody(request, data, len, index, total);
    }
        );

        // GET /api/firmware/status — progress query
        server->on(
            "/api/firmware/status",
            HTTP_GET,
            [this](AsyncWebServerRequest *request) {
        this->handleStatusRequest(request);
    }
        );
    }

    void FirmwareUpdateServer::handleUploadBody(
    AsyncWebServerRequest *request,
    uint8_t *              data,
    size_t                 len,
    size_t                 index,
    size_t                 total
    )
    {
        if (index == 0) {
            D_PRINTFLN("[FW] upload start, total=%u bytes", (unsigned)total);
            uploadFile = Lightnet::Fs::open(FIRMWARE_PATH, "w");

            if (!uploadFile) {
                D_PRINTLN("[FW] ERROR: cannot open /panel_fw.bin for writing");
                request->send(507, "application/json",
                              "{\"error\":\"filesystem write failed\"}");

                return;
            }
        }

        if (uploadFile) {
            uploadFile.write(data, len);
        }

        if (index + len == total) {
            if (uploadFile) {
                uploadFile.close();
                D_PRINTFLN("[FW] upload done, %u bytes written", (unsigned)total);
            }
        }
    }

    void FirmwareUpdateServer::handleStatusRequest(AsyncWebServerRequest *request)
    {
        PanelFlasher::Status s = flasher->getStatus();
        char body[192];

        if (s.hasError) {
            size_t pos = (size_t)snprintf(body, sizeof(body),
                                          "{\"state\":\"error\",\"panel\":%d,\"total\":%d,\"error\":",
                                          s.panelIdx, s.totalPanels);

            if (pos < sizeof(body)) {
                pos = Lightnet::jsonAppendQuotedString(body, sizeof(body), pos, s.errorMsg);

                if (pos != (size_t)-1 && pos + 2 < sizeof(body)) {
                    body[pos++] = '}';
                    body[pos]   = '\0';
                }
            }
        } else {
            const char *stateStr = "idle";

            switch (s.state) {
                case PanelFlasher::State::ENTER_BL:
                case PanelFlasher::State::WAIT_BL:   stateStr = "connecting";
                    break;
                case PanelFlasher::State::FLASHING:  stateStr = "flashing";
                    break;
                case PanelFlasher::State::VERIFY:    stateStr = "verifying";
                    break;
                case PanelFlasher::State::NEXT_PANEL: stateStr = "flashing";
                    break;
                case PanelFlasher::State::DONE:      stateStr = "done";
                    break;
                default:                             stateStr = "idle";
                    break;
            }

            snprintf(body, sizeof(body),
                     "{\"state\":\"%s\",\"panel\":%d,\"total\":%d,\"progress\":%d}",
                     stateStr, s.panelIdx, s.totalPanels, s.progressPct);
        }

        request->send(200, "application/json", body);
    }

#endif  // LIGHTNET_TARGET_CONTROLLER
