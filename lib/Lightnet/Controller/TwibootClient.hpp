#pragma once

#ifdef LIGHTNET_TARGET_CONTROLLER

#include <Arduino.h>
#include <Wire.h>

// Implements the twiboot_for_arduino I2C host protocol for programming ATmega panels.
// Uses Wire directly — bypasses LNBus, which handles only Lightnet Protocol packets.
//
// Targets the twiboot_for_arduino fork (firmware/twiboot_for_arduino/).
// That fork enters bootloader only when EEPROM[510]==0xB007 on startup (set by
// BootloaderBridge via a WDT hardware reset). TWI address is 0x29 (compiled in via
// -DTWI_ADDRESS=0x29).
//
// writePage() sends the 128-byte page in two 64-byte chunks (68 bytes each with
// the 4-byte header). This fits within the default 128-byte Wire TX buffer.
class TwibootClient {
public:
    struct ChipInfo {
        uint8_t  bootloaderVersion;
        uint16_t pageSize;   // bytes per flash page (128 for ATmega328)
        uint16_t flashSize;  // programmable bytes (app section, not bootloader)
    };

    // Confirm twiboot is present at address. Retries up to maxRetries × retryDelayMs ms.
    // Optionally fills *info. Returns true on success.
    bool connect(uint8_t address, ChipInfo *info = nullptr,
                 uint8_t maxRetries = 20, uint16_t retryDelayMs = 50);

    // Write data[0..size-1] to flash, page by page.
    // Calls progressCb(pct) after each page if provided.
    bool programFlash(uint8_t address, const uint8_t *data, size_t size,
                      void (*progressCb)(uint8_t pct) = nullptr);

    // Read back flash and compare against data[0..size-1]. Optional verify pass.
    bool verifyFlash(uint8_t address, const uint8_t *data, size_t size);

    // Tell twiboot to jump to the application.
    bool startApp(uint8_t address);

    // twiboot's hardcoded I²C slave address (TWI_ADDRESS in main.c).
    // All panels run twiboot at this address regardless of their app-mode index.
    static const uint8_t  TWIBOOT_ADDRESS = 0x29;

    // ATmega328 flash geometry
    static const uint16_t PAGE_SIZE = 128;

    // Write 128 bytes to flash at byteAddr (must be PAGE_SIZE-aligned).
    bool writePage(uint8_t address, uint16_t byteAddr, const uint8_t *data);

    // Read 128 bytes from flash at byteAddr into buf.
    bool readPage(uint8_t address, uint16_t byteAddr, uint8_t *buf);

private:
    // twiboot command bytes (orempel/twiboot protocol)
    // Write flash: SLA+W, 0x02, 0x01, addrH, addrL, {128 bytes}, STO
    // Read flash:  SLA+W, 0x02, 0x01, addrH, addrL, STO, SLA+R, {N bytes}, STO
    static const uint8_t CMD_SWITCH_APPLICATION = 0x01;
    static const uint8_t CMD_ACCESS_MEMORY      = 0x02;  // used for both read and write
    static const uint8_t MEMTYPE_FLASH          = 0x01;  // memory type byte required by twiboot

    static const uint8_t CMD_WAIT        = 0x00;  // abort boot timeout / verify presence
    static const uint8_t ARG_APPLICATION = 0x80;  // used with CMD_SWITCH_APPLICATION

    uint8_t sendCmd(uint8_t address, uint8_t cmd, const uint8_t *args, uint8_t argLen);
};

#endif  // LIGHTNET_TARGET_CONTROLLER
