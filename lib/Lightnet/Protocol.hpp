#pragma once

#include <Arduino.h>

#define MAX_PACKET_SIZE 50
#define MIN_PACKET_SIZE sizeof(Protocol::PacketMeta)

namespace Protocol
{
    const uint8_t VERSION = 1;

    const uint8_t CONTROLLER_ADDRESS = 100;

    enum colorMode_t {
        COLOR_MODE_RGB,
        COLOR_MODE_HSB
    };

    enum packetType_t {
        PACKET_REGISTER_PANEL,
        PACKET_REGISTER_PANEL_RESPONSE,
        PACKET_TURN_ON_OFF,
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
        uint8_t hue;
        uint8_t saturation;
        uint8_t brightness;
    } ColorHSB;

    typedef struct
    {
        colorMode_t mode;
        union
        {
            ColorRGB rgb;
            ColorHSB hsb;
        };
    } Color;

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

    typedef struct
    {
        PacketMeta meta;
        uint8_t parentEdge;
        uint8_t edgesNumber;
    } RegisterPanel;

    typedef struct
    {
        PacketMeta meta;
        uint8_t panelId;
    } RegisterPanelResponse;

    typedef struct
    {
        PacketMeta meta;
        uint8_t on;
    } TurnOnOff;

    typedef struct
    {
        PacketMeta meta;
        Color color;
        uint8_t brightness;
    } SetColorAndBrightness;

    uint8_t validatePacket(void *packet, uint8_t size);
    void setPacketMeta(void *packet, packetType_t type);
}
