#pragma once

// ClockedLed — drives the single on-panel LED over a two-wire clock+data protocol
// (APA102/SK9822-style), replacing the WS2812/FastLED NRZ-timing driver, per
// docs/hardware/schematics/Panel.png: LED_SCK/LED_MOSI on PC4/PC5 (freed from I2C SDA/SCL by
// the relay redesign).
//
// Unlike WS2812, a clocked protocol samples data on the clock edge rather than decoding pulse
// *widths*, so it needs no interrupt-disable window around the transmission — this is what
// removes the FastLED interrupt-disable hazard the hardware redesign plan §4 flags: a ~30 us
// cli() window during a WS2812 update could overrun the relay USART's small hardware RX buffer;
// a clocked protocol never needs that disable in the first place.
//
// Raw AVR register access only (no Arduino API), matching EdgeUartTransport's style — this
// keeps the door open for the eventual bare-metal panel build (§10) without needing a second
// rewrite later.

#include <stdint.h>
#include <avr/io.h>

class ClockedLed
{
    public:
        void begin();

        // Sends one LED's colour with a 5-bit hardware brightness (0..31). One call fully
        // updates the single on-panel LED, including the start/end frames.
        void show(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness5bit);

    private:
        void writeByte(uint8_t value);
};

extern ClockedLed LNLed;
