#pragma once

#include <Arduino.h>
#include "Protocol.hpp"
#include "Macros.hpp"
#include <Wire.h>
#include "Crc.hpp"

class LightnetBus
{
    public:
        typedef void (*onPacketReceived_t)(Protocol::PacketMeta *packet);
        typedef void (*onPacketRequested_t)();

        LightnetBus();
        uint8_t registerPanel(uint8_t edgesNumber, uint8_t parentEdge);
        void begin(uint8_t address);
        void begin();
        void end();
        void setOnPacketReceived(onPacketReceived_t callback);
        void setOnPacketRequested(onPacketRequested_t callback);

    private:
        static onPacketReceived_t onPacketReceivedCallback;
        static onPacketRequested_t onPacketRequestedCallback;

        static void onReceive(int size);
        static void onRequest();
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

extern LightnetBus LNBus;
