#pragma once

#define SET_BIT(x, y)                       ((x) |= (1 << (y)))
#define CLEAR_BIT(x, y)                     ((x) &= ~(1 << (y)))
#define BIT_IS_SET(x, y)                    ((x) & (1 << (y)))
#define BIT_IS_CLEAR(x, y)                  (!BIT_IS_SET(x, y))
#define READ_BIT(val, bit)                 (BIT_IS_SET(val, bit) >> bit)

#define DDR(x)                              (_SFR_IO8(_SFR_IO_ADDR(x)-1))
#define PIN(x)                              (_SFR_IO8(_SFR_IO_ADDR(x)-2))
#define SET_PIN_AS_OUTPUT(port, bit)        SET_BIT(DDR(port), bit)
#define SET_PIN_AS_INPUT(port, bit)         CLEAR_BIT(DDR(port), bit); CLEAR_BIT(port, bit)
#define SET_PIN_AS_INPUT_UP(port, bit)      CLEAR_BIT(DDR(port), bit); SET_BIT(port, bit)
#define READ_PIN(port, bit)                 READ_BIT(PIN(port), bit)
#define PIN_IS_HIGH(port, bit)              READ_PIN(port, bit)
#define PIN_IS_LOW(port, bit)               (!READ_PIN(port, bit))
#define SET_PIN_HIGH(port, bit)             SET_BIT(port, bit)
#define SET_PIN_LOW(port, bit)              CLEAR_BIT(port, bit)
