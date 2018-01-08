#pragma once

#include <Arduino.h>

#define MAX_PACKET_SIZE 50
#define MIN_PACKET_SIZE sizeof(Protocol::PacketMeta)

namespace Protocol
{
    static const uint8_t VERSION = 1;

    static const uint8_t CONTROLLER_ADDRESS = 100;

    static const uint8_t PACKET_REGISTER_PANEL = 1;
    static const uint8_t PACKET_REGISTER_PANEL_RESPONSE = 2;

    typedef struct
    {
        uint8_t type;
        uint8_t protocolVersion;
    } PacketHeader;

    typedef struct
    {
        PacketHeader header;
        uint16_t headerCrc;
    } PacketMeta;

    typedef struct
    {
        PacketMeta meta;
        uint8_t parentBorder;
        uint8_t bordersNumber;
    } RegisterPanel;

    typedef struct
    {
        PacketMeta meta;
        uint8_t panelId;
    } RegisterPanelResponse;


    uint8_t validatePacket(void *packet, uint8_t size);
    void setPacketMeta(void *packet, uint8_t type);
}
