#pragma once

#if !IS_ESP

    #include <avr/eeprom.h>
    #include <avr/io.h>
    #include <avr/interrupt.h>
    #include "../Common/Protocol.hpp"
    #include "bootloader/BootloaderProtocol.hpp"

    // Coordinates with RelayBootloader.cpp (lib/Lightnet/Panel/bootloader/), the relay network's
    // own OTA bootloader — see BootloaderProtocol.hpp for the full EEPROM contract.
    //
    // Entry protocol:
    //   1. Write this panel's assigned index + parent edge + boot magic 0xB007 to EEPROM.
    //   2. Software jump to BOOTLOADER_START (0x7000) — see the note below on why this is a
    //      direct jump rather than a hardware WDT reset.
    //   3. RelayBootloader reads EEPROM, sees 0xB007 → stays resident, clears the magic so the
    //      next power-cycle boots the app normally.
    //   4. The controller drives OTA over the relay to this panel's persisted parent edge.
    namespace BootloaderBridge {
        static constexpr uint8_t ENTRY_TOKEN  = Protocol::BOOTLOADER_ENTRY_TOKEN;

        inline void prepareAndReset(uint8_t parentEdgeIndex, uint16_t assignedPanelIndex)
        {
            // Write the bootloader's EEPROM handoff so it stays resident, listening on the one
            // edge that leads back toward the controller — see BootloaderProtocol.hpp for why
            // the bootloader can't discover this for itself.
            eeprom_busy_wait();
            eeprom_write_byte((uint8_t *)BootloaderProtocol::EEPROM_PARENT_EDGE_ADDR, parentEdgeIndex);
            eeprom_busy_wait();
            eeprom_write_word((uint16_t *)BootloaderProtocol::EEPROM_PANEL_INDEX_ADDR, assignedPanelIndex);
            eeprom_busy_wait();
            eeprom_write_word((uint16_t *)BootloaderProtocol::EEPROM_MAGIC_ADDR, BootloaderProtocol::ENTRY_MAGIC);
            eeprom_busy_wait();

            cli();

            // Disable peripherals whose interrupts could fire after the bootloader calls sei().
            // With IVSEL=0 (default, not changed by a software jump), any enabled interrupt
            // would be dispatched to the *app's* IVT, which could corrupt bootloader state.
            // ATmega328PB has two TWI peripherals, named TWCR0/TWCR1 in raw avr-libc (no
            // single-TWI TWCR alias the way MiniCore provided); plain ATmega328P has only one,
            // still named TWCR. The bootloader doesn't use TWI at all, but disabling it here
            // costs nothing and matches the same defensive intent as PCICR/TIMSK1 below.
            #if defined(__AVR_ATmega328PB__)
                TWCR0 = 0; // disable TWI0
            #else
                TWCR  = 0; // disable TWI
            #endif
            PCICR = 0; // disable pin-change interrupts
            TIMSK1 = 0; // disable Timer1 interrupts

            // Defensively zero the bootloader's .data/.bss range before the jump, regardless of
            // whether its own init chain re-does this — RelayBootloader.cpp explains why nothing
            // there depends on either this sweep or that init chain for correctness.
            for (uint16_t a = 0x0100; a < 0x0500; a++) {
                *(volatile uint8_t *)a = 0;
            }

            // Software jump to word address 0x3800 (byte address 0x7000 = BOOTLOADER_START).
            // main() finds the EEPROM magic → stays resident.
            //
            // A direct jump rather than a hardware WDT reset -- a WDT reset leaves SRAM content
            // from the app still resident with no crt0-style re-init to clear it, so a stray
            // non-zero global would misbehave immediately (see RelayBootloader.cpp's own
            // explicit-runtime-init discussion). A software jump sidesteps that question
            // entirely rather than depending on it working out.
            typedef void (*bootloader_t)(void) __attribute__((noreturn));
            ((bootloader_t)0x3800)();
            __builtin_unreachable();
        }
    }

#endif // !IS_ESP
