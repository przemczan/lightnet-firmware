#pragma once

#include <Arduino.h>
#include "Protocol.hpp"
#include "Wire.h"
#include "Debug.hpp"

#define IS_ESP8266 defined(ARDUINO_ARCH_ESP8266)
#define IS_ESP32 defined(ARDUINO_ARCH_ESP32)
#define IS_ESP (IS_ESP8266 || IS_ESP32)

#if IS_ESP
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
        uint8_t sendPacket(uint8_t address, void *packet, uint8_t size, Protocol::packetType_t type);
        uint8_t sendResponsePacket(void *packet, uint8_t size, Protocol::packetType_t type);
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
