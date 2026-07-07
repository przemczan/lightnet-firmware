#pragma once

// LightnetBus — SIM_MODE-only in-memory dispatch to SimPanelManager (Sim/LightnetBusSim.cpp).
// This class exists so sim panels, which only ever respond to LightnetBus-routed commands, and the shared
// ControllerPacketSink/PanelsController code that talks to them, don't need a SIM_MODE-specific
// API.

#include <stdint.h>
#include "Protocol.hpp"

class LightnetBus
{
    public:
        // Fired before a packet is dispatched to sim panels. Lets the WebSocket layer mirror
        // outbound packets to clients. Null = no-op.
        typedef void (*onPacketSent_t)(uint8_t address, const Protocol::PacketMeta *packet, uint8_t size);

        LightnetBus();

        uint8_t sendPacket(uint8_t address, const Protocol::PacketMeta *packet, uint8_t size, bool end);
        uint8_t sendData(uint8_t address, const Protocol::PacketMeta *data, uint8_t size, bool end);
        uint8_t sendPacketAck(uint8_t address, const Protocol::PacketMeta *packet, uint8_t size);
        uint8_t sendPacketNack(uint8_t address, const Protocol::PacketMeta *packet, uint8_t size);
        uint8_t sendPacketWithResponse(
            uint8_t                     address,
            const Protocol::PacketMeta *packet,
            uint8_t                     packetSize,
            Protocol::PacketMeta *      responseBuffer,
            uint8_t                     responseSize
        );
        void setOnPacketSent(onPacketSent_t callback);

    private:
        onPacketSent_t onPacketSentCallback = nullptr;
};

extern LightnetBus LNBus;
