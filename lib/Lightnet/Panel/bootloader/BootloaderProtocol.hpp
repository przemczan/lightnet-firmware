#pragma once

// BootloaderProtocol — the AVR EEPROM contract between the running application
// (Panel/BootloaderBridge.hpp) and RelayBootloader.cpp, resident at BOOTLOADER_START.
//
// The bootloader has no discovery/topology of its own — it's a from-scratch ~4 KB image with no
// link to PanelDiscovery or anything else in the app build. Everything it needs to talk to the
// one edge that leads back toward the controller has to be handed to it by the application,
// immediately before the software jump: which edge that is (the mux can't be re-discovered by
// the bootloader itself), and this panel's own assigned index (so it can tell an OTA packet
// meant for it apart from ordinary app-mode traffic still flooding past on the same edge).
//
// ENTRY_MAGIC/EEPROM_MAGIC_ADDR are this bootloader's own convention (0xB007 at byte offset 510)
// — not shared with, or dependent on, any other bootloader implementation.

#include <stdint.h>

namespace BootloaderProtocol {
    const uint16_t ENTRY_MAGIC = 0xB007;

    // uint16_t, written last (see BootloaderBridge::prepareAndReset) — its presence is what
    // tells the bootloader "stay resident" instead of falling straight through to the app.
    const uint16_t EEPROM_MAGIC_ADDR = 510;

    // uint16_t — this panel's own PanelDiscoveryDriver::assignedPanelIndex() at the moment it
    // was told to enter the bootloader. Defensive: only one panel is ever resident in its
    // bootloader at a time, so this is a belt-and-braces filter against stray OTA traffic, not
    // the primary addressing mechanism.
    const uint16_t EEPROM_PANEL_INDEX_ADDR = 508;

    // uint8_t — this panel's own PanelDiscovery::parentEdge() at the moment it was told to enter
    // the bootloader. The one EdgeUartTransport channel (mux select + TX enable) the bootloader
    // listens/replies on for its entire resident lifetime; it never switches edges.
    const uint16_t EEPROM_PARENT_EDGE_ADDR = 507;

    const uint8_t NO_PARENT_EDGE = 0xFF;

    // Same LIGHTNET_TRUNK_BAUD as EdgeUartTransport::begin()'s app-mode baud (src/panel.config.hpp,
    // force-included in both the app and bootloader envs -- see platformio.ini) — the bootloader
    // shares the wire with panels still running the application, so the two can never diverge.
    // Changing the config baud requires re-burning the bootloader, not just reflashing the app.
    const uint32_t UART_BAUD = LIGHTNET_TRUNK_BAUD;
}  // namespace BootloaderProtocol
