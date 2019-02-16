#include "Crc.hpp"

uint16_t crc16Update(uint16_t crc, uint8_t a)
{
  uint16_t i;
  crc ^= a;
  for (i = 0; i < 8; ++i)
  {
    if (crc & 1)crc = (crc >> 1) ^ 0xA001;
    else crc = (crc >> 1);
  }
  return crc;
}

uint16_t crc16(void *data, uint16_t size)
{
    uint16_t crc = 0xFFFF;
    uint8_t *source = (uint8_t *)data;

    while (size--) {
        crc = crc16Update(crc, *source++);
    }

    return crc;
}
