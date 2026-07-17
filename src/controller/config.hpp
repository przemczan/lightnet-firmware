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
        #define CONTROLLER_TRUNK_RX_PIN 11
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
#elif defined(ARDUINO_ESP32C3_DEV)
    // Unbranded ESP32-C3-MINI-1 clone board (silkscreen just says "ESP32-C3 module", no
    // WEMOS/Lolin/Espressif branding) -- see env:controller_esp32_c3's own comment in
    // platformio.ini for how the physical-UART-bridge-vs-native-USB question was settled.
    //
    // This board plugs into the *same physical socket* on the Controller relay board as the S2
    // Mini above -- docs/hardware/schematics/Controller.net's generic "U1 WEMOS" schematic
    // symbol wires PRX/PTX/PE/PTXEN to that footprint's classic D1-mini position names (A0, D0,
    // D5, D6), not to any particular chip's real GPIO. What actually matters is which GPIO each
    // *specific* plugged-in module exposes at those same physical positions, which the S2 Mini
    // branch above already establishes as positions 2/3/4/5 of the header row starting at
    // EN/RST. Reading this clone's silkscreen in that same position order (position 1 = EN,
    // position 8 = 3V3, matching the S2 Mini's own endpoints) gives GPIO9/1/2/3 for
    // positions 2/3/4/5 -- i.e. CONTROLLER_TRUNK_RX_PIN/TX_PIN, PANELS_POWER_PIN, and
    // CONTROLLER_TRUNK_OE_PIN below. An earlier version of this branch picked GPIO4/5/6/10
    // instead -- any GPIO this board's header happened to expose, without regard to *position* --
    // which compiled fine but wasn't wired to anything on the relay board at all (no panel power
    // switching, no trunk traffic).
    //
    // GPIO9 (trunk RX) and GPIO2 (panel power) are both C3 strapping pins -- not swappable for
    // something safer, since the relay board's traces fix which position carries which signal,
    // not this file. GPIO9 is the boot-mode strap (low at reset -> download mode); the trunk
    // idles high, so this should boot normally, but if the board intermittently fails to boot or
    // drops into download mode on its own, this is the first place to look -- not a firmware bug.
    //
    // LED_PIN has no confirmed onboard LED behind it and isn't part of the relay board's PLED1
    // net (that one's hardwired to +3.3V through a resistor, not GPIO-controlled) -- just a free
    // GPIO for a status output, unrelated to the position-derived pins above.
    #ifndef CONTROLLER_TRUNK_RX_PIN
        // normally 9 by the design, but 9 was fried on most of my board because of power spike issue
        #define CONTROLLER_TRUNK_RX_PIN 4
    #endif
    #ifndef CONTROLLER_TRUNK_TX_PIN
        #define CONTROLLER_TRUNK_TX_PIN 1
    #endif
    #ifndef CONTROLLER_TRUNK_OE_PIN
        #define CONTROLLER_TRUNK_OE_PIN 3
    #endif
    #ifndef LED_PIN
        #define LED_PIN 7
    #endif
    #ifndef PANELS_POWER_PIN
        #define PANELS_POWER_PIN 2
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
