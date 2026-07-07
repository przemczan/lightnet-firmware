#ifndef LIGHTNET_TARGET_CONTROLLER
#include "ClockedLed.hpp"

namespace {
    // LED_SCK / LED_MOSI, PC4/PC5 (docs/hardware/schematics/Panel.png).
    const uint8_t SCK_BIT  = (1 << PC4);
    const uint8_t MOSI_BIT = (1 << PC5);
}

void ClockedLed::begin()
{
    DDRC  |= SCK_BIT | MOSI_BIT;
    PORTC &= ~(SCK_BIT | MOSI_BIT);
}

void ClockedLed::writeByte(uint8_t value)
{
    for (int8_t bit = 7; bit >= 0; bit--) {
        if (value & (1 << bit)) {
            PORTC |= MOSI_BIT;
        } else {
            PORTC &= ~MOSI_BIT;
        }

        PORTC |= SCK_BIT;    // data is sampled on the rising edge
        PORTC &= ~SCK_BIT;
    }
}

void ClockedLed::show(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness5bit)
{
    // Start frame.
    for (uint8_t i = 0; i < 4; i++) {
        this->writeByte(0x00);
    }

    // One LED frame: 3 header bits (1) + 5-bit brightness, then B, G, R (APA102/SK9822 order).
    this->writeByte(0xE0 | (brightness5bit & 0x1F));
    this->writeByte(b);
    this->writeByte(g);
    this->writeByte(r);

    // End frame — enough clock edges to latch a single LED.
    for (uint8_t i = 0; i < 4; i++) {
        this->writeByte(0xFF);
    }
}

ClockedLed LNLed;
#endif  // LIGHTNET_TARGET_CONTROLLER
