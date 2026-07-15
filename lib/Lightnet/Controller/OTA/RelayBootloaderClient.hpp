#pragma once

#ifdef LIGHTNET_TARGET_CONTROLLER
    #ifndef SIM_MODE

        #include <Arduino.h>
        #include "../Relay/ControllerRelayPacketSink.hpp"

        // RelayBootloaderClient — drives RelayBootloader.cpp (lib/Lightnet/Panel/bootloader/) over
        // the relay trunk, the only OTA path this controller build supports.
        // Addresses a panel by its own assigned index (PacketHeader.targetPanelIndex)
        // rather than a fixed bus address — every panel's resident bootloader answers to its own index,
        // not a shared constant, since there is no physical bus address to share.
        //
        // No read-back verify pass exists here — each chunk's own CRC-16 (checked by the bootloader
        // before it ever reaches boot_page_fill(), see PacketBootloaderWriteChunk's class comment) is
        // the integrity guarantee for this path instead. Adding a real flash read-back would need its
        // own new packet type (PACKET_BOOTLOADER_READ_CHUNK or similar) — not built, a real gap, not
        // an oversight.
        //
        // UNVALIDATED HARDWARE — no bench spike has run, and there is no sim path either (sim panels
        // don't implement any bootloader protocol) — "builds clean" is the bar this can be held to
        // today.
        class RelayBootloaderClient
        {
            public:
                static const uint16_t PAGE_SIZE = 128; // ATmega328/PB SPM_PAGESIZE

                // Per-chunk send attempts -- see writeChunk()'s comment on why retrying is both
                // necessary (the relay loses the occasional frame by design) and safe (idempotent).
                static const uint8_t WRITE_ATTEMPTS = 3;

                // Extra gap between attempts. The bare ACK_TIMEOUT_MS (300 ms) retry cadence sits
                // uncomfortably close to the relay panels' own quiet-window debug flush
                // (LightnetPanel::RELAY_QUIET_MS = 200 ms after the lost frame, up to ~70 ms of
                // blocking bit-banged lines) -- a deliberately non-round offset decorrelates the
                // retries from that window and from the 1 s heartbeat, so consecutive attempts
                // can't keep landing in the same blackout.
                static const uint16_t RETRY_GAP_MS = 133;

                explicit RelayBootloaderClient(Lightnet::ControllerRelayPacketSink &sink);

                // Confirms the bootloader is resident at panelIndex and listening. Retries up to
                // maxRetries times, retryDelayMs apart.
                bool connect(uint16_t panelIndex, uint8_t maxRetries = 5, uint16_t retryDelayMs = 50);

                // Writes 128 bytes to flash at byteAddr (must be PAGE_SIZE-aligned) as two
                // BOOTLOADER_CHUNK_SIZE (64 B) chunks — matches the bootloader's own per-page commit
                // trigger (a chunk that exactly completes a page commits it immediately).
                bool writePage(uint16_t panelIndex, uint16_t byteAddr, const uint8_t *data);

                // Tells the bootloader to commit any pending page and jump to the application.
                // Fire-and-forget — the panel jumps away immediately and never replies.
                void startApp(uint16_t panelIndex);

            private:
                Lightnet::ControllerRelayPacketSink &sink;

                bool writeChunk(uint16_t panelIndex, uint16_t address, const uint8_t *data, uint8_t length);
        };

    #endif  // !SIM_MODE
#endif  // LIGHTNET_TARGET_CONTROLLER
