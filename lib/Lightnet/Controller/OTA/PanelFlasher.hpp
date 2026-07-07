#pragma once

// OTA only exists on real hardware — the relay trunk replaced the shared I2C bus entirely, so
// there is no bootloader transport of any kind under SIM_MODE (sim panels don't implement a
// bootloader protocol either). See RelayBootloaderClient's own class comment for the wire side.
#if defined(LIGHTNET_TARGET_CONTROLLER) && !defined(SIM_MODE)

    #include <Arduino.h>
    #include "../../Utils/Fs/Fs.hpp"
    #include "../Panels/PanelsController.hpp"
    #include "../Panels/PanelsInitializer.hpp"
    #include "RelayBootloaderClient.hpp"

    // Orchestrates OTA firmware updates for all discovered panels, over the relay trunk via
    // RelayBootloaderClient.
    //
    // Call startFlashing() to begin; then call run() every main loop iteration.
    // The state machine is fully non-blocking — individual steps complete within
    // a single run() call, feeding the ESP watchdog via yield() between pages.
    //
    // Flashes leaves first, root last (currentPanelAddress() walks getPanels() back to front —
    // see its own comment). This is a deliberate reboot-safety property, not an arbitrary choice:
    // a protocol-version bump means every panel not yet reflashed only responds to discovery
    // probes/traffic that share its own (still old) protocolVersion — everything except
    // PACKET_RESET_DEVICE/PACKET_ENTER_BOOTLOADER is rejected on a mismatch (see
    // Protocol::isVersionExemptType()). If the controller itself reboots mid-campaign before its
    // own update, rediscovery runs at whatever version the controller currently has. Root-last
    // means the controller and every not-yet-flashed panel share that version throughout, so the
    // whole not-yet-flashed portion of the tree stays fully discoverable and reachable across
    // such a reboot; the only panels a reboot can drop out of discovery are ones already
    // finished. Flashing root-first (discovery order) has the opposite property: a reboot can
    // permanently orphan the entire not-yet-flashed subtree behind whichever panel it first
    // finds already updated, since that panel rejects the older probe and nothing behind it can
    // be discovered — not recoverable without physically reflashing it over ISP.
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
                WAIT_BL, // polling the bootloader until it responds (or timeout)
                FLASHING, // programming pages
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
                PanelsController *     ctrl,
                PanelsInitializer *    init,
                RelayBootloaderClient *relayBoot
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
            // BootloaderBridge::prepareAndReset() is a direct software jump, not a WDT reset, so
            // there's no reset-propagation delay to wait out — the settle time is just the
            // ENTER_BOOTLOADER packet's own relay transit (depth-dependent, up to the plan's own
            // worst-case depth-50 estimate) plus the panel's EEPROM writes before the jump. Not
            // bench-validated; conservative placeholder pending real hardware.
            static const uint16_t WAIT_TIMEOUT_MS     = 3000;
            static const uint16_t ENTER_BL_SETTLE_MS  = 300;

            PanelsController *ctrl;
            PanelsInitializer *init;
            RelayBootloaderClient *relayBoot;

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

#endif  // LIGHTNET_TARGET_CONTROLLER && !SIM_MODE
