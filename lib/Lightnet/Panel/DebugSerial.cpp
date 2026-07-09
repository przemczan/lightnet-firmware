#ifndef LIGHTNET_TARGET_CONTROLLER
#include "DebugSerial.hpp"

#include <avr/io.h>
#include <avr/pgmspace.h>
#include <stdlib.h>
#include <util/delay.h>

namespace {
    const unsigned long DEBUG_SERIAL_BAUD = 9600UL;
    const double BIT_US            = 1000000.0 / DEBUG_SERIAL_BAUD;

    // The one actual bit-banging primitive -- transmits exactly the 8 bits given, framed with a
    // start and stop bit. Every public debugSerialWrite() overload funnels through this one byte
    // at a time; none of them treat a raw uint8_t/uint16_t/long argument as a byte value to send
    // directly (that would emit an unprintable binary byte instead of readable decimal text).
    void writeRawByte(uint8_t byte)
    {
        PORTD &= ~(1 << PD7);  // start bit
        _delay_us(BIT_US);

        for (uint8_t i = 0; i < 8; i++) {
            if (byte & 0x01) {
                PORTD |= (1 << PD7);
            } else {
                PORTD &= ~(1 << PD7);
            }

            byte >>= 1;
            _delay_us(BIT_US);
        }

        PORTD |= (1 << PD7);  // stop bit
        _delay_us(BIT_US);
    }
}  // namespace

namespace Lightnet {
    void debugSerialBegin()
    {
        DDRD  |= (1 << PD7);
        PORTD |= (1 << PD7);  // idle high
    }

    void debugSerialWrite(char c)
    {
        writeRawByte((uint8_t)c);
    }

    void debugSerialWrite(bool value)
    {
        writeRawByte(value ? '1' : '0');
    }

    void debugSerialWrite(uint8_t value)
    {
        char buf[4];  // "255" + null

        ultoa(value, buf, 10);
        debugSerialWrite((const char *)buf);
    }

    void debugSerialWrite(uint16_t value)
    {
        char buf[6];  // "65535" + null

        ultoa(value, buf, 10);
        debugSerialWrite((const char *)buf);
    }

    void debugSerialWrite(long value)
    {
        char buf[12];  // "-2147483648" + null

        ltoa(value, buf, 10);
        debugSerialWrite((const char *)buf);
    }

    void debugSerialWrite(const char *str)
    {
        while (*str) {
            writeRawByte((uint8_t)*str++);
        }
    }

    void debugSerialWrite(const FlashStringHelper *str)
    {
        PGM_P p = (PGM_P)str;
        char c;

        while ((c = (char)pgm_read_byte(p++)) != '\0') {
            writeRawByte((uint8_t)c);
        }
    }
}  // namespace Lightnet

#endif  // LIGHTNET_TARGET_CONTROLLER
