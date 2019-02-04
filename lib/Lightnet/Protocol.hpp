#pragma once

#include <Arduino.h>

namespace Protocol
{
    const uint8_t VERSION = 1;

    const uint8_t MAX_PACKET_SIZE = 50;

    const uint8_t CONTROLLER_ADDRESS = 200;
    const uint8_t POLLING_ADDRESS = 201;

    enum colorMode_t {
        COLOR_MODE_RGB
    };

    enum packetType_t {
        PACKET_INITIALIZATION_POLL = 1,
        PACKET_REGISTER_EDGE,
        PACKET_TURN_ON_OFF,
        PACKET_SET_COLOR,
        PACKET_SET_BRIGHTNESS,
        PACKET_SET_COLOR_AND_BRIGHTNESS
    };

    typedef struct
    {
        uint8_t r;
        uint8_t g;
        uint8_t b;
    } ColorRGB;

    typedef struct
    {
        colorMode_t mode;
        union
        {
            ColorRGB rgb;
        };
    } Color;

    // BEGIN Common packet structures
    typedef struct
    {
        packetType_t type;
        uint8_t protocolVersion;
    } PacketHeader;

    typedef struct
    {
        PacketHeader header;
        uint16_t headerCrc;
    } PacketMeta;
    // END

    // BEGIN Packets definitions
    typedef struct
    {
        PacketMeta meta;
        uint16_t panelIndex;
    } PacketInitializationPoll;

    typedef struct
    {
        PacketMeta meta;
        uint16_t panelIndex;
        uint16_t edgeIndex;
    } PacketRegisterEdge;

    typedef struct
    {
        PacketMeta meta;
        uint8_t on;
    } PacketTurnOnOff;

    typedef struct
    {
        PacketMeta meta;
        Color color;
    } PacketSetColor;

    typedef struct
    {
        PacketMeta meta;
        uint8_t brightness;
    } PacketSetBrightness;

    typedef struct
    {
        PacketMeta meta;
        Color color;
        uint8_t brightness;
    } PacketSetColorAndBrightness;
    // END

    uint8_t validatePacket(void *packet, uint8_t size);
    void setPacketMeta(void *packet, packetType_t type);

    const uint8_t MIN_PACKET_SIZE = sizeof(PacketMeta);
}
