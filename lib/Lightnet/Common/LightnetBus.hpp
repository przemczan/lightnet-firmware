#pragma once

#include <Arduino.h>
#include "Protocol.hpp"
#include "Wire.h"
#include "../Utils/Debug.hpp"
#include "consts.hpp"

#if IS_ESP8266
    #include <twi.h>
#endif

class LightnetBus
{
    const uint32_t BUS_FREQUENCY = 400000;

    public:
        typedef void (*onPacketReceived_t)(Protocol::PacketMeta *packet, int size);
        typedef void (*onPacketRequested_t)();

        LightnetBus();

        void begin(uint8_t address);
        void begin(uint8_t sdaPin, uint8_t sclPin, uint8_t address);
        void begin();
        void begin(uint8_t sdaPin, uint8_t sclPin);
        void end();
        uint8_t sendPacket(uint8_t address, void *packet, uint8_t size, Protocol::packetType_t type, bool end);
        uint8_t sendData(uint8_t address, void *data, uint8_t size, bool end);
        uint8_t sendPacketAck(uint8_t address, void *packet, uint8_t size, Protocol::packetType_t type);
        uint8_t sendPacketNack(uint8_t address, void *packet, uint8_t size, Protocol::packetType_t type);
        uint8_t sendPacketWithResponse(
            uint8_t address,
            void *packet,
            uint8_t packetSize,
            Protocol::packetType_t packetType,
            void *responseBuffer,
            uint8_t responseSize
        );
        uint8_t sendResponsePacket(void *packet, uint8_t size, Protocol::packetType_t type);
        uint8_t sendResponseData(void *data, uint8_t size);
        uint8_t requestData(uint8_t address, void *buffer, uint8_t maxSize);
        uint8_t requestPacket(uint8_t address, void *buffer, uint8_t size);
        void setOnPacketReceived(onPacketReceived_t callback);
        void setOnPacketRequested(onPacketRequested_t callback);
        void flush();

    private:
        onPacketReceived_t onPacketReceivedCallback;
        onPacketRequested_t onPacketRequestedCallback;
        void onReceive(int size);
        void onRequest();
        static void onReceiveService(int size);
        static void onRequestService();
};

extern LightnetBus LNBus;
