#pragma once

#include <Arduino.h>
#include "Protocol.hpp"
#include "Macros.hpp"
#include <Wire.h>
#include "Bus.hpp"
#include "Config.hpp"
#include "Crc.hpp"

class Bus
{
    public:
        uint8_t registerPanel(uint8_t bordersNumber, uint8_t parentBorder);
        void begin(uint8_t address);
        void begin();
        void end();

    private:
        uint8_t sendPacket(uint8_t address, void *packet, uint8_t size, uint8_t type);
        uint8_t requestPacket(uint8_t address, void *buffer, uint8_t size);
        uint8_t sendPacketWithResponse(
            uint8_t address,
            void *packet,
            uint8_t packetSize,
            uint8_t packetType,
            void *responseBuffer,
            uint8_t responseSize
        );
        void flush();
};
