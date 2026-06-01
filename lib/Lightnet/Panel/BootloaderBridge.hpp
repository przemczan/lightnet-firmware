#pragma once

#if !IS_ESP

    #include <avr/eeprom.h>
    #include "../Common/Protocol.hpp"

    // Coordinates with twiboot_for_arduino (the fork).
    //
    // Entry protocol:
    //   1. Write boot magic word 0xB007 to EEPROM[510].
    //   2. Trigger a hardware WDT reset.
    //   3. BOOTRST fuse → MCU starts at 0x7000 (fork bootloader).
    //   4. Fork reads EEPROM[510], sees 0xB007 → stays in bootloader,
    //      clears the magic so the next power-cycle boots the app normally.
    //   5. Controller connects at TWI_ADDRESS (0x29) and programs the flash.
    //
    // The fork's .init0/.init3 disable the WDT immediately on startup,
    // so there is no WDT boot-loop.
    namespace BootloaderBridge {
        static constexpr uint8_t ENTRY_TOKEN  = Protocol::BOOTLOADER_ENTRY_TOKEN;

        inline void prepareAndReset(uint8_t /*i2cAddress*/)
        {
            // Write the fork's EEPROM boot-magic so it stays in bootloader mode.
            eeprom_busy_wait();
            eeprom_write_word((uint16_t *)510, 0xB007);
            eeprom_busy_wait();

            cli();

            // Disable peripherals whose interrupts could fire after the fork calls sei().
            // With IVSEL=0 (default, not changed by a software jump), any enabled interrupt
            // would be dispatched to the *app's* IVT, which could corrupt fork state.
            TWCR  = 0; // disable TWI
            PCICR = 0; // disable pin-change interrupts
            TIMSK1 = 0; // disable Timer1 interrupts

            // Zero twiboot's .data/.bss range — no crt0 means no automatic init.
            // Covers boot_timeout, cmd, buf[], addr, page_dirty, etc.
            for (uint16_t a = 0x0100; a < 0x0500; a++) {
                *(volatile uint8_t *)a = 0;
            }

            // Software jump to fork at word address 0x3800 (byte address 0x7000 = BOOTLOADER_START).
            // The fork's init sections (.init0 / .init3) run and disable WDT (no-op here since
            // we never enabled it). main() finds EEPROM magic 0xB007 → stays in bootloader.
            //
            // We do NOT use a hardware WDT reset because after a WDT reset SRAM is preserved
            // but .data/.bss are not re-initialized (no crt0), causing the fork's
            // bootloadLoopCount to retain an app-side garbage value and exit immediately.
            typedef void (*twiboot_t)(void) __attribute__((noreturn));
            ((twiboot_t)0x3800)();
            __builtin_unreachable();
        }
    }

#endif // !IS_ESP
