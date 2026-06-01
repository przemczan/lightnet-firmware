#include "SerialFirmwareReceiver.hpp"

#ifdef LIGHTNET_TARGET_CONTROLLER

    const uint8_t SerialFirmwareReceiver::MAGIC[4]         = { 'L', 'N', 'F', 'W' };
    const char *SerialFirmwareReceiver::FIRMWARE_PATH     = "/panel_fw.bin";

    SerialFirmwareReceiver::SerialFirmwareReceiver(PanelFlasher *flasher)
        : flasher(flasher)
    {
    }

    void SerialFirmwareReceiver::run()
    {
        // Non-blocking: scan for magic bytes only. Once all 4 are matched, hand
        // off to receiveBlocking() which owns Serial until the transfer ends.
        while (Serial.available() > 0) {
            uint8_t b = (uint8_t)Serial.read();

            if (b == MAGIC[magicPos]) {
                magicPos++;

                if (magicPos == 4) {
                    headerPos = 0;
                    state     = State::HEADER;
                    receiveBlocking();

                    return;
                }
            } else {
                magicPos = (b == MAGIC[0]) ? 1 : 0;
            }
        }
    }

    void SerialFirmwareReceiver::receiveBlocking()
    {
        unsigned long deadline = millis() + TRANSFER_TIMEOUT_MS;

        while (state != State::IDLE) {
            if ((long)(millis() - deadline) > 0) {
                replyError("timeout");
                reset();

                return;
            }

            if (Serial.available() == 0) {
                yield(); // feed ESP watchdog; also lets WiFi background tasks run
                continue;
            }

            processByte((uint8_t)Serial.read());
        }
    }

    void SerialFirmwareReceiver::processByte(uint8_t b)
    {
        switch (state) {
            // ----------------------------------------------------------------
            case State::HEADER:
                headerBuf[headerPos++] = b;

                if (headerPos == 4) {
                    firmwareSize = (uint32_t)headerBuf[0]
                                   | ((uint32_t)headerBuf[1] << 8)
                                   | ((uint32_t)headerBuf[2] << 16)
                                   | ((uint32_t)headerBuf[3] << 24);

                    if (firmwareSize == 0 || firmwareSize > MAX_FIRMWARE_SIZE) {
                        replyError("invalid size");
                        reset();
                        break;
                    }

                    if (flasher->isActive()) {
                        replyError("flash in progress");
                        reset();
                        break;
                    }

                    outFile = SPIFFS.open(FIRMWARE_PATH, "w");

                    if (!outFile) {
                        replyError("SPIFFS open failed");
                        reset();
                        break;
                    }

                    bytesWritten = 0;
                    runningCrc   = 0xFFFF;
                    Serial.println("READY");
                    state = State::DATA;
                }

                break;

            // ----------------------------------------------------------------
            case State::DATA:
                outFile.write(b);
                runningCrc = crc16Update(runningCrc, b);
                bytesWritten++;

                if (bytesWritten == firmwareSize) {
                    outFile.close();
                    crcPos = 0;
                    state  = State::CRC_BYTES;
                }

                break;

            // ----------------------------------------------------------------
            case State::CRC_BYTES:
                crcBuf[crcPos++] = b;

                if (crcPos == 2) {
                    uint16_t receivedCrc = (uint16_t)crcBuf[0]
                                           | ((uint16_t)crcBuf[1] << 8);

                    if (receivedCrc != runningCrc) {
                        replyError("CRC mismatch");
                        reset();
                        break;
                    }

                    flasher->startFlashing(FIRMWARE_PATH);

                    if (flasher->getStatus().hasError) {
                        replyError(flasher->getStatus().errorMsg);
                    } else {
                        replyOk();
                    }

                    reset();
                }

                break;

            default:
                break;
        }
    }

    // ---------------------------------------------------------------------------

    void SerialFirmwareReceiver::reset()
    {
        if (outFile) outFile.close();

        state        = State::IDLE;
        magicPos     = 0;
        headerPos    = 0;
        firmwareSize = 0;
        bytesWritten = 0;
        crcPos       = 0;
        runningCrc   = 0xFFFF;
    }

    void SerialFirmwareReceiver::replyOk()
    {
        Serial.println("OK");
    }

    void SerialFirmwareReceiver::replyError(const char *msg)
    {
        Serial.print("ERR:");
        Serial.println(msg);
    }

    uint16_t SerialFirmwareReceiver::crc16Update(uint16_t crc, uint8_t b)
    {
        crc ^= b;

        for (uint8_t i = 0; i < 8; i++) {
            if (crc & 1) crc = (crc >> 1) ^ 0xA001;
            else crc = (crc >> 1);
        }

        return crc;
    }

#endif  // LIGHTNET_TARGET_CONTROLLER
