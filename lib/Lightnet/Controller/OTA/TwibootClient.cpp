#include "TwibootClient.hpp"

#ifdef LIGHTNET_TARGET_CONTROLLER

    #include "../../Utils/Debug.hpp"

    bool TwibootClient::connect(
    uint8_t   address,
    ChipInfo *info,
    uint8_t   maxRetries,
    uint16_t  retryDelayMs
    )
    {
        for (uint8_t attempt = 0; attempt < maxRetries; attempt++) {
            // CMD_WAIT (0x00) aborts the boot countdown and verifies the bootloader
            // is listening. Do NOT use CMD_SWITCH_APPLICATION + BOOTTYPE_BOOTLOADER
            // (0x01 0x00) — the fork interprets that as "enter bootloader from app",
            // which writes EEPROM[510]=0xB007 and triggers another WDT reset → bootloop.
            uint8_t err = sendCmd(address, CMD_WAIT, nullptr, 0);

            if (err == 0) {
                DEBUG_IF(DEBUG_FLASHER, D_PRINTFLN("[TWIBOOT] connected @ 0x%02X (attempt %d)", address, attempt + 1));

                if (info) {
                    info->pageSize  = PAGE_SIZE;
                    info->flashSize = 28 * 1024;
                    info->bootloaderVersion = 0;
                }

                return true;
            }

            DEBUG_IF(DEBUG_FLASHER, D_PRINTFLN("[TWIBOOT] connect attempt %d/%d @ 0x%02X failed (err=%d)",
                                             attempt + 1, maxRetries, address, err));
            delay(retryDelayMs);
        }

        DEBUG_IF(DEBUG_FLASHER, D_PRINTFLN("[TWIBOOT] connect failed after %d attempts", maxRetries));

        return false;
    }

    bool TwibootClient::programFlash(
    uint8_t        address,
    const uint8_t *data,
    size_t         size,
    void (*        progressCb)(uint8_t pct)
    )
    {
        uint16_t numPages = (uint16_t)((size + PAGE_SIZE - 1) / PAGE_SIZE);

        DEBUG_IF(DEBUG_FLASHER, D_PRINTFLN("[TWIBOOT] programming %u pages (%u bytes)", numPages, (unsigned)size));

        for (uint16_t page = 0; page < numPages; page++) {
            uint16_t byteAddr = page * PAGE_SIZE;

            if (!writePage(address, byteAddr, data + byteAddr)) {
                DEBUG_IF(DEBUG_FLASHER, D_PRINTFLN("[TWIBOOT] write failed at page %u (addr 0x%04X)", page, byteAddr));

                return false;
            }

            yield(); // feed ESP watchdog between pages

            if (progressCb) {
                progressCb((uint8_t)((uint32_t)(page + 1) * 100 / numPages));
            }
        }

        DEBUG_IF(DEBUG_FLASHER, D_PRINTLN("[TWIBOOT] programFlash done"));

        return true;
    }

    bool TwibootClient::verifyFlash(uint8_t address, const uint8_t *data, size_t size)
    {
        uint16_t numPages = (uint16_t)((size + PAGE_SIZE - 1) / PAGE_SIZE);
        uint8_t buf[PAGE_SIZE];

        DEBUG_IF(DEBUG_FLASHER, D_PRINTFLN("[TWIBOOT] verifying %u pages", numPages));

        for (uint16_t page = 0; page < numPages; page++) {
            uint16_t byteAddr = page * PAGE_SIZE;

            if (!readPage(address, byteAddr, buf)) {
                DEBUG_IF(DEBUG_FLASHER, D_PRINTFLN("[TWIBOOT] read failed at page %u (addr 0x%04X)", page, byteAddr));

                return false;
            }

            if (memcmp(buf, data + byteAddr, PAGE_SIZE) != 0) {
                DEBUG_IF(DEBUG_FLASHER, D_PRINTFLN("[TWIBOOT] verify mismatch at page %u", page));

                return false;
            }

            yield();
        }

        DEBUG_IF(DEBUG_FLASHER, D_PRINTLN("[TWIBOOT] verify OK"));

        return true;
    }

    bool TwibootClient::startApp(uint8_t address)
    {
        uint8_t arg = ARG_APPLICATION;
        uint8_t err = sendCmd(address, CMD_SWITCH_APPLICATION, &arg, 1);

        if (err != 0) {
            DEBUG_IF(DEBUG_FLASHER, D_PRINTFLN("[TWIBOOT] startApp failed @ 0x%02X (err=%d)", address, err));

            return false;
        }

        DEBUG_IF(DEBUG_FLASHER, D_PRINTFLN("[TWIBOOT] app started @ 0x%02X", address));

        return true;
    }

    // ---------------------------------------------------------------------------
    // Private helpers
    // ---------------------------------------------------------------------------

    bool TwibootClient::writePage(uint8_t address, uint16_t byteAddr, const uint8_t *data)
    {
        // The fork streams any-size chunks; it auto-commits a flash page when the
        // write address crosses a SPM_PAGESIZE (128-byte) boundary on STOP.
        // We send the page as two 64-byte chunks (4 header + 64 data = 68 bytes each),
        // which fits within the default 128-byte Wire TX buffer with no buffer hacks.
        //
        //   SLA+W, 0x02, 0x01, addrH, addrL, {64 bytes}, STO  <- chunk 0
        //   SLA+W, 0x02, 0x01, addrH, addrL+64, {64 bytes}, STO  <- chunk 1 → triggers page write
        static const uint8_t CHUNK = 64;

        for (uint8_t c = 0; c < PAGE_SIZE / CHUNK; c++) {
            uint16_t chunkAddr = byteAddr + (uint16_t)c * CHUNK;

            Wire.beginTransmission(address);
            Wire.write(CMD_ACCESS_MEMORY);
            Wire.write(MEMTYPE_FLASH);
            Wire.write((uint8_t)(chunkAddr >> 8));
            Wire.write((uint8_t)(chunkAddr & 0xFF));
            Wire.write(data + c * CHUNK, CHUNK);

            uint8_t err = Wire.endTransmission();

            if (err != 0) {
                DEBUG_IF(DEBUG_FLASHER, D_PRINTFLN("[TWIBOOT] writePage chunk %d err=%d @ 0x%04X", c, err, chunkAddr));

                return false;
            }

            delay(10); // bootloader processes chunk; last chunk triggers SPM (~4.5 ms)
        }

        return true;
    }

    bool TwibootClient::readPage(uint8_t address, uint16_t byteAddr, uint8_t *buf)
    {
        // Set read address: SLA+W, 0x02 (CMD_ACCESS_MEMORY), 0x01 (MEMTYPE_FLASH), addrH, addrL, STO
        Wire.beginTransmission(address);
        Wire.write(CMD_ACCESS_MEMORY);
        Wire.write(MEMTYPE_FLASH);
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

    uint8_t TwibootClient::sendCmd(
    uint8_t        address,
    uint8_t        cmd,
    const uint8_t *args,
    uint8_t        argLen
    )
    {
        Wire.beginTransmission(address);
        Wire.write(cmd);

        if (args && argLen > 0) {
            Wire.write(args, argLen);
        }

        return Wire.endTransmission();
    }

#endif  // LIGHTNET_TARGET_CONTROLLER
