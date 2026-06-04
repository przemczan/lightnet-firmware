#pragma once

#ifdef LIGHTNET_TARGET_CONTROLLER

    #include <Arduino.h>
    #include "../../Utils/Fs/Fs.hpp"
    #include "PanelFlasher.hpp"

    // Receives a panel firmware binary over the USB serial port, stores it to
    // the filesystem, then triggers PanelFlasher.
    //
    // Wire protocol (little-endian):
    //   PC → [4B magic 'L','N','F','W'][4B size][size bytes data][2B CRC-16]
    //   Controller → "READY\n"  once header is validated (PC may then stream data)
    //   Controller → "OK\n"     on success (flashing begins asynchronously)
    //   Controller → "ERR:…\n"  on any error
    //
    // CRC-16: polynomial 0xA001, init 0xFFFF (matches Crc.hpp crc16()).
    // Run via the tools/flash_panels_serial.py helper script.
    //
    // Once the 4-byte magic is matched, run() enters a blocking receive loop that
    // holds until the full transfer completes, fails, or times out. This lets the
    // receiver work even when the main loop is busy with long-running demo animations.
    class SerialFirmwareReceiver
    {
        public:
            explicit SerialFirmwareReceiver(PanelFlasher *flasher);

            // Call every main loop iteration. Non-blocking while scanning for magic;
            // blocks (with watchdog yields) once magic is matched until transfer ends.
            void run();

        private:
            enum class State {
                IDLE, MAGIC, HEADER, DATA, CRC_BYTES
            };

            static const uint8_t MAGIC[4];
            static const char *FIRMWARE_PATH;
            static const uint32_t MAX_FIRMWARE_SIZE   = 28 * 1024;
            static const uint32_t TRANSFER_TIMEOUT_MS = 30000;
            // Write firmware in chunks to avoid per-byte LittleFS overhead, which
            // causes flush stalls long enough to overflow the 256-byte UART ring buffer.
            static const uint16_t WRITE_CHUNK         = 256;

            PanelFlasher *flasher;
            State state       = State::IDLE;
            uint8_t magicPos    = 0;
            uint8_t headerBuf[4];
            uint8_t headerPos   = 0;
            uint32_t firmwareSize = 0;
            uint32_t bytesWritten = 0;
            uint16_t runningCrc  = 0xFFFF;
            uint8_t crcBuf[2];
            uint8_t crcPos      = 0;
            File outFile;
            uint8_t writeBuf[WRITE_CHUNK];
            uint16_t writeBufLen = 0;

            // Blocking loop entered after magic match; returns when state == IDLE.
            void receiveBlocking();
            // Process one byte through HEADER/DATA/CRC_BYTES states.
            void processByte(uint8_t b);

            void reset();
            void replyOk();
            void replyError(const char *msg);

            static uint16_t crc16Update(uint16_t crc, uint8_t byte);
    };

#endif  // LIGHTNET_TARGET_CONTROLLER
