#pragma once

#ifdef LIGHTNET_TARGET_CONTROLLER

    #include <Arduino.h>
    #include <ESPAsyncWebServer.h>
    #include "../../Utils/Fs/Fs.hpp"
    #include "PanelFlasher.hpp"

    // Registers HTTP endpoints for firmware management:
    //
    //   POST /api/firmware/panels   — upload raw firmware binary; flashing starts immediately
    //   GET  /api/firmware/status   — returns JSON with current flash progress
    //
    // The upload is streamed directly to /panel_fw.bin to avoid holding
    // the entire binary in RAM.  Once the upload completes, PanelFlasher::startFlashing()
    // is called; the actual I2C programming runs asynchronously in the main loop.
    class FirmwareUpdateServer
    {
        public:
            FirmwareUpdateServer(AsyncWebServer *server, PanelFlasher *flasher);

        private:
            PanelFlasher *flasher;
            File uploadFile;

            void handleUploadBody(
                AsyncWebServerRequest *request,
                uint8_t *              data,
                size_t                 len,
                size_t                 index,
                size_t                 total
            );

            void handleStatusRequest(AsyncWebServerRequest *request);
    };

#endif  // LIGHTNET_TARGET_CONTROLLER
