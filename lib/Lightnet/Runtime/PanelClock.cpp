#include "PanelClock.hpp"

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>

namespace {
    volatile uint32_t millisCounter = 0;
}

namespace Lightnet {
    void clockInit()
    {
        TCCR0A = (1 << WGM01);                // CTC mode (WGM02:0 = 010)
        TCCR0B = (1 << CS01) | (1 << CS00);    // prescaler 64 (CS02:0 = 011)
        OCR0A  = 249;                          // 250 counts x 4 us = 1 ms at 16 MHz
        TIMSK0 = (1 << OCIE0A);

        millisCounter = 0;
    }

    uint32_t millis()
    {
        uint8_t savedSREG = SREG;

        cli();

        uint32_t value = millisCounter;

        SREG = savedSREG;

        return value;
    }

    void delay(uint32_t ms)
    {
        while (ms--) {
            _delay_ms(1);
        }
    }

    void delayMicroseconds(uint16_t us)
    {
        while (us--) {
            _delay_us(1);
        }
    }
}  // namespace Lightnet

ISR(TIMER0_COMPA_vect)
{
    millisCounter++;
}
