#pragma once

#ifdef LIGHTNET_TARGET_CONTROLLER

    #include <Arduino.h>
    #include "../../Utils/Fs/Fs.hpp"
    #include "../Panels/PanelsController.hpp"
    #include "../Panels/PanelsInitializer.hpp"
    #include "TwibootClient.hpp"

    // Orchestrates OTA firmware updates for all discovered panels.
    //
    // Call startFlashing() to begin; then call run() every main loop iteration.
    // The state machine is fully non-blocking — individual steps complete within
    // a single run() call, feeding the ESP watchdog via yield() between pages.
    //
    // After all panels are flashed the controller must be restarted so that
    // PanelsInitializer can re-run discovery (panels reboot into the new firmware
    // and wait for a fresh welcome ping).
    class PanelFlasher
    {
        public:
            enum class State {
                IDLE,
                ENTER_BL, // sending PACKET_ENTER_BOOTLOADER to current panel
                WAIT_BL, // polling twiboot until it responds (or timeout)
                FLASHING, // programming pages via TwibootClient
                VERIFY, // optional read-back verification
                NEXT_PANEL, // advance to next panel or finish
                DONE,
                ERROR,
            };

            struct Status {
                State   state;
                uint8_t panelIdx; // 0-based index into the discovered panels list
                uint8_t totalPanels;
                uint8_t progressPct; // progress within the current panel (0-100)
                bool    hasError;
                char    errorMsg[64];
            };

            PanelFlasher(
                PanelsController * ctrl,
                PanelsInitializer *init,
                TwibootClient *    twiboot
            );

            // Load firmware from firmwarePath on the filesystem and begin flashing all panels.
            // Safe to call again after DONE/ERROR to retry.
            void startFlashing(const char *firmwarePath);

            // Drive the state machine. Call every main loop iteration.
            void run();

            Status getStatus() const
            {
                return status;
            }

            bool   isActive()  const
            {
                return (status.state != State::IDLE)
                       && (status.state != State::DONE)
                       && (status.state != State::ERROR);
            }

        private:
            static const uint16_t TWIBOOT_WAIT_TIMEOUT_MS = 3000;
            // WDT fires ~15 ms after wdt_enable() in BootloaderBridge; the fork bootloader
            // then does _delay_ms(200) before initialising TWI. Total ~215 ms from the
            // enterBootloader() I²C write until 0x29 is ready; 300 ms gives safe margin.
            static const uint16_t ENTER_BL_SETTLE_MS      = 300;

            PanelsController *ctrl;
            PanelsInitializer *init;
            TwibootClient *twiboot;

            Status status;
            uint32_t stateEnteredAt = 0;

            // Firmware image buffered page-by-page from the filesystem; not held in RAM as a whole.
            char firmwarePath[64];
            size_t firmwareSize = 0;
            uint16_t currentPage  = 0;
            uint16_t totalPages   = 0;
            File flashFile; // kept open across FLASHING pages to avoid per-page open overhead

            void transition(State next);
            void setError(const char *msg);
            void advancePanel();

            uint8_t currentPanelAddress() const;
    };

#endif  // LIGHTNET_TARGET_CONTROLLER
