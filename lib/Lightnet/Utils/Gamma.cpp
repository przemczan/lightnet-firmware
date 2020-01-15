#include <Gamma.hpp>

uint8_t gammaValueR(uint8_t value)
{
    return pgm_read_byte(&gammaR[value]);
}

uint8_t gammaValueG(uint8_t value)
{
    return pgm_read_byte(&gammaG[value]);
}

uint8_t gammaValueB(uint8_t value)
{
    return pgm_read_byte(&gammaB[value]);
}