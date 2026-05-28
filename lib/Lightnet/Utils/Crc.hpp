#pragma once

#include <stdint.h>

uint16_t crc16Update(uint16_t crc, uint8_t a);
uint16_t crc16(void *data, uint16_t size);
