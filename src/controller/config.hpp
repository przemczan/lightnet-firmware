#pragma once
#include "../controller.config.hpp"

// Mutual exclusion: SIM_MODE and DEMO_MODE cannot both be active
#if defined(SIM_MODE) && DEMO_MODE
    #error "SIM_MODE and DEMO_MODE cannot both be enabled"
#endif

// SimPanelManager — controller + SIM_MODE builds only
#if defined(SIM_MODE) && defined(LIGHTNET_TARGET_CONTROLLER)
    #include "SimPanelManager.hpp"
#endif

// Platform-specific pin defaults (override in controller.config.hpp if needed).
//
// CONTROLLER_TRUNK_RX_PIN/CONTROLLER_TRUNK_TX_PIN feed Serial1, the relay's single physical trunk
// port (Controller/Relay/ControllerEdgeTransport) — a second, genuinely free hardware UART, which
// is why ESP8266 controller targets are retired (only one usable hardware UART, already the
// debug/log Serial port; see platformio.ini).
#if defined(ARDUINO_LOLIN_S2_MINI)
    // Bench-verified against the populated board (docs/hardware/schematics/Controller.png's PTX/
    // PRXv3 nets) — the schematic's "U1 WEMOS" symbol uses generic D0/D1/A0-style pin labels that
    // don't match this module's real silkscreen (it only ever exposes raw IOxx/GPIO numbers, no
    // D-alias at all), so the label text there is misleading; these are the real GPIOs.
    #ifndef CONTROLLER_TRUNK_RX_PIN
        #define CONTROLLER_TRUNK_RX_PIN 3
    #endif
    #ifndef CONTROLLER_TRUNK_TX_PIN
        #define CONTROLLER_TRUNK_TX_PIN 5
    #endif
    // Gates U4 (the trunk line driver) onto the shared half-duplex wire — bench-verified spare
    // GPIO, wired directly to U4's OE# pin (see ControllerEdgeTransport's class comment for why
    // this can't just be tied to GND).
    #ifndef CONTROLLER_TRUNK_OE_PIN
        #define CONTROLLER_TRUNK_OE_PIN 9
    #endif
    #ifndef LED_PIN
        #define LED_PIN 15
    #endif
    #ifndef PANELS_POWER_PIN
        #define PANELS_POWER_PIN 7
    #endif
#elif defined(ARDUINO_ARCH_ESP32)
    #ifndef CONTROLLER_TRUNK_RX_PIN
        #define CONTROLLER_TRUNK_RX_PIN 12
    #endif
    #ifndef CONTROLLER_TRUNK_TX_PIN
        #define CONTROLLER_TRUNK_TX_PIN 13
    #endif
    // Not bench-verified against real hardware (unlike the S2 Mini pins above) — no populated
    // board of this variant exists yet.
    #ifndef CONTROLLER_TRUNK_OE_PIN
        #define CONTROLLER_TRUNK_OE_PIN 14
    #endif
    #ifndef LED_PIN
        #define LED_PIN 2
    #endif
    #ifndef PANELS_POWER_PIN
        #define PANELS_POWER_PIN 21
    #endif
#else
    #error \
    "Unsupported controller platform -- ESP8266 controller targets are retired (see platformio.ini); this codebase now targets ESP32-class boards only."
#endif
