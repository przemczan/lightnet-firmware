#include "Mem.hpp"

void memcpyToVolatile(volatile uint8_t *dest, uint8_t *src, int size)
{
    while (size--) {
        dest[size] = src[size];
    }
}

void memcpyFromVolatile(uint8_t *dest, volatile uint8_t *src, int size)
{
    while (size--) {
        dest[size] = src[size];
    }
}
