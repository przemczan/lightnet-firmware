#pragma once

#if !IS_ESP

#include <avr/eeprom.h>
#include <avr/wdt.h>
#include "../Common/Protocol.hpp"

// Coordinates with twiboot: on PACKET_ENTER_BOOTLOADER, the application writes
// the panel's current I2C address and an entry magic to two reserved EEPROM bytes,
// then triggers a watchdog reset. twiboot reads these on startup to decide whether
// to enter programming mode or jump to the app immediately.
//
// These offsets and magic values MUST match the twiboot build configuration.
namespace BootloaderBridge {

    static constexpr uint8_t EEPROM_I2C_ADDR_OFFSET = 0;   // twiboot TWI slave address
    static constexpr uint8_t EEPROM_MODE_FLAG_OFFSET = 1;   // bootloader entry flag

    static constexpr uint8_t ENTRY_MAGIC = 0x42;  // written to EEPROM[1] to request bootloader entry
    // ENTRY_TOKEN is Protocol::BOOTLOADER_ENTRY_TOKEN — shared with controller side
    static constexpr uint8_t ENTRY_TOKEN = Protocol::BOOTLOADER_ENTRY_TOKEN;

    // Write I2C address + entry magic to EEPROM, then reset via watchdog.
    // twiboot will see the flag, stay in programming mode, and clear it after a
    // successful flash. If power is lost mid-transfer, the flag survives and
    // twiboot will re-enter programming mode on the next reset (never boots
    // corrupted firmware).
    inline void prepareAndReset(uint8_t i2cAddress) {
        // Address first, then flag — if power fails between the two writes,
        // twiboot falls back to its compiled-in default address.
        eeprom_busy_wait();
        eeprom_update_byte((uint8_t *)EEPROM_I2C_ADDR_OFFSET, i2cAddress);
        eeprom_busy_wait();
        eeprom_update_byte((uint8_t *)EEPROM_MODE_FLAG_OFFSET, ENTRY_MAGIC);
        eeprom_busy_wait();

        // Watchdog reset (32 ms) — same sequence as PACKET_RESET_DEVICE.
        MCUSR   =  MCUSR  & B11110111;
        WDTCSR  =  WDTCSR | B00011000;
        WDTCSR  =  B00000001;
        WDTCSR  =  WDTCSR | B01000000;
        MCUSR   =  MCUSR  & B11110111;
        delay(50);
    }

}

#endif // !IS_ESP
