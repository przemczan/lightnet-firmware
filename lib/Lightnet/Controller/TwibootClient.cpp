#include "TwibootClient.hpp"

#ifdef LIGHTNET_TARGET_CONTROLLER

#include "../Utils/Debug.hpp"

bool TwibootClient::connect(uint8_t address, ChipInfo *info,
                             uint8_t maxRetries, uint16_t retryDelayMs)
{
    for (uint8_t attempt = 0; attempt < maxRetries; attempt++) {
        uint8_t arg = ARG_BOOTLOADER;
        uint8_t err = sendCmd(address, CMD_SWITCH_APPLICATION, &arg, 1);

        if (err == 0) {
            PRINTF("[TWIBOOT] connected @ 0x%02X (attempt %d)\n", address, attempt + 1);

            if (info) {
                info->pageSize  = PAGE_SIZE;
                info->flashSize = 28 * 1024;
                // Read 2 info bytes twiboot makes available after a successful write
                Wire.requestFrom(address, (uint8_t)2);
                info->bootloaderVersion = Wire.available() ? Wire.read() : 0;
                if (Wire.available()) Wire.read();  // discard second byte
            }
            return true;
        }

        PRINTF("[TWIBOOT] connect attempt %d/%d @ 0x%02X failed (err=%d)\n",
               attempt + 1, maxRetries, address, err);
        delay(retryDelayMs);
    }

    PRINTF("[TWIBOOT] connect failed after %d attempts\n", maxRetries);
    return false;
}

bool TwibootClient::programFlash(uint8_t address, const uint8_t *data, size_t size,
                                  void (*progressCb)(uint8_t pct))
{
    uint16_t numPages = (uint16_t)((size + PAGE_SIZE - 1) / PAGE_SIZE);

    PRINTF("[TWIBOOT] programming %u pages (%u bytes)\n", numPages, (unsigned)size);

    for (uint16_t page = 0; page < numPages; page++) {
        uint16_t byteAddr = page * PAGE_SIZE;

        if (!writePage(address, byteAddr, data + byteAddr)) {
            PRINTF("[TWIBOOT] write failed at page %u (addr 0x%04X)\n", page, byteAddr);
            return false;
        }

        yield();  // feed ESP watchdog between pages

        if (progressCb) {
            progressCb((uint8_t)((uint32_t)(page + 1) * 100 / numPages));
        }
    }

    PRINTLN("[TWIBOOT] programFlash done");
    return true;
}

bool TwibootClient::verifyFlash(uint8_t address, const uint8_t *data, size_t size)
{
    uint16_t numPages = (uint16_t)((size + PAGE_SIZE - 1) / PAGE_SIZE);
    uint8_t  buf[PAGE_SIZE];

    PRINTF("[TWIBOOT] verifying %u pages\n", numPages);

    for (uint16_t page = 0; page < numPages; page++) {
        uint16_t byteAddr = page * PAGE_SIZE;

        if (!readPage(address, byteAddr, buf)) {
            PRINTF("[TWIBOOT] read failed at page %u (addr 0x%04X)\n", page, byteAddr);
            return false;
        }

        if (memcmp(buf, data + byteAddr, PAGE_SIZE) != 0) {
            PRINTF("[TWIBOOT] verify mismatch at page %u\n", page);
            return false;
        }

        yield();
    }

    PRINTLN("[TWIBOOT] verify OK");
    return true;
}

bool TwibootClient::startApp(uint8_t address)
{
    uint8_t arg = ARG_APPLICATION;
    uint8_t err = sendCmd(address, CMD_SWITCH_APPLICATION, &arg, 1);

    if (err != 0) {
        PRINTF("[TWIBOOT] startApp failed @ 0x%02X (err=%d)\n", address, err);
        return false;
    }

    PRINTF("[TWIBOOT] app started @ 0x%02X\n", address);
    return true;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

bool TwibootClient::writePage(uint8_t address, uint16_t byteAddr, const uint8_t *data)
{
    uint16_t offset = 0;

    while (offset < PAGE_SIZE) {
        uint8_t  chunkSize = (uint8_t)min((uint16_t)MAX_CHUNK_DATA,
                                          (uint16_t)(PAGE_SIZE - offset));
        uint16_t chunkAddr = byteAddr + offset;

        Wire.beginTransmission(address);
        Wire.write(CMD_WRITE_FLASH);
        Wire.write((uint8_t)(chunkAddr >> 8));
        Wire.write((uint8_t)(chunkAddr & 0xFF));
        Wire.write(data + offset, chunkSize);

        uint8_t err = Wire.endTransmission();
        if (err != 0) {
            PRINTF("[TWIBOOT] writePage NACK at offset %u (err=%d)\n", offset, err);
            return false;
        }

        offset += chunkSize;
        delay(1);  // brief pause between chunks
    }

    // twiboot commits when its 128-byte buffer is full; SPM takes ~4.5 ms
    delay(6);
    return true;
}

bool TwibootClient::readPage(uint8_t address, uint16_t byteAddr, uint8_t *buf)
{
    // Set read address
    Wire.beginTransmission(address);
    Wire.write(CMD_READ_FLASH);
    Wire.write((uint8_t)(byteAddr >> 8));
    Wire.write((uint8_t)(byteAddr & 0xFF));
    if (Wire.endTransmission() != 0) return false;

    // Read PAGE_SIZE bytes in 32-byte chunks
    uint16_t received = 0;
    while (received < PAGE_SIZE) {
        uint8_t chunk = (uint8_t)min((uint16_t)32, (uint16_t)(PAGE_SIZE - received));
        uint8_t got   = Wire.requestFrom(address, chunk);

        if (got == 0) return false;

        for (uint8_t i = 0; i < got && received < PAGE_SIZE; i++) {
            buf[received++] = Wire.read();
        }
    }

    return received == PAGE_SIZE;
}

uint8_t TwibootClient::sendCmd(uint8_t address, uint8_t cmd,
                                const uint8_t *args, uint8_t argLen)
{
    Wire.beginTransmission(address);
    Wire.write(cmd);
    if (args && argLen > 0) {
        Wire.write(args, argLen);
    }
    return Wire.endTransmission();
}

#endif  // LIGHTNET_TARGET_CONTROLLER
