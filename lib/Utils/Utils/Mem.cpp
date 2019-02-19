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

void dumpMem(uint8_t *mem, size_t size)
{
    uint16_t index = 0;

    while (index++ < size) {
        if (!(index % 20)) {
            PRINT("\n");
        }
        PRINTF("%02X ", *mem++);
    }
    PRINT("\n");
}
