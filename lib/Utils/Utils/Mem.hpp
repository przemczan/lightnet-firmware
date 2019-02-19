#pragma once

#include <Arduino.h>
#include "Debug.hpp"

void memcpyToVolatile(volatile uint8_t *dest, uint8_t *src, int size);

void memcpyFromVolatile(uint8_t *dest, volatile uint8_t *src, int size);

void dumpMem(uint8_t *mem, size_t size);
