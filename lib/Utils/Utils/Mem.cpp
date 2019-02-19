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

void dumpMem(uint8_t *mem, size_t size, uint8_t width)
{
    uint16_t index = 0;

    PRINT("\n");

    while (index++ < size) {
        PRINTF("%02X ", *mem++);
        if (!(index % width)) {
            PRINT("\n");
        }
    }
    PRINT("\n");
}
