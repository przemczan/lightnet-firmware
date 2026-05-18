#pragma once

#ifdef LIGHTNET_TARGET_CONTROLLER

#include <Arduino.h>
#include <Wire.h>

// Implements the twiboot I2C host protocol for programming ATmega panels.
// Uses Wire directly — bypasses LNBus, which handles only Lightnet Protocol packets.
//
// Command byte values must match the compiled twiboot binary (orempel/twiboot).
// Reference: https://github.com/orempel/twiboot
//
// Flash page size for ATmega328: 128 bytes.
// Pages are written by streaming data in ≤29-byte Wire chunks; twiboot
// auto-commits the page when its 128-byte internal buffer fills.
// SPM write takes ~4.5 ms — a post-page delay is included.
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

private:
    // twiboot command bytes — verify these against the compiled twiboot source
    static const uint8_t CMD_SWITCH_APPLICATION = 0x01;
    static const uint8_t CMD_WRITE_FLASH        = 0x02;
    static const uint8_t CMD_READ_FLASH         = 0x03;

    static const uint8_t ARG_BOOTLOADER  = 0x00;  // used with CMD_SWITCH_APPLICATION
    static const uint8_t ARG_APPLICATION = 0x80;  // used with CMD_SWITCH_APPLICATION

    // ATmega328 flash geometry
    static const uint16_t PAGE_SIZE      = 128;
    // Wire buffer is 32 bytes; 3 bytes consumed by cmd + addr_high + addr_low
    static const uint8_t  MAX_CHUNK_DATA = 29;

    bool    writePage(uint8_t address, uint16_t byteAddr, const uint8_t *data);
    bool    readPage(uint8_t address, uint16_t byteAddr, uint8_t *buf);
    uint8_t sendCmd(uint8_t address, uint8_t cmd, const uint8_t *args, uint8_t argLen);
};

#endif  // LIGHTNET_TARGET_CONTROLLER
