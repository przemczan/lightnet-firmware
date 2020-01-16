#pragma once

#include <Arduino.h>
#include "../Utils/Crc.hpp"
#include <FastLED.h>

#define PACK __attribute__((__packed__))

namespace Protocol
{
    const uint16_t VERSION = 3;
    const uint8_t MAX_PACKET_SIZE = 32;
    const uint8_t PULLING_ADDRESS = 120;

    enum packetType_t: uint8_t {
        PACKET_NOOP = 0,
        PACKET_ACK = 1,
        PACKET_INITIALIZATION_PULL = 2,
        PACKET_REGISTER_EDGE = 3,
        PACKET_TURN_ON_OFF = 4,
        PACKET_SET_COLOR = 5,
        PACKET_SET_BRIGHTNESS = 6,
        PACKET_SET_COLOR_AND_BRIGHTNESS = 7,
        PACKET_REGISTER_EDGE_ACK = 8,
        PACKET_PANEL_EDGE_INFO = 9,
        PACKET_FETCH_STATE = 10,
        PACKET_PANEL_CONFIGURATION = 11,
        PACKET_RESET_DEVICE = 200,
    };

    typedef struct PACK {
        uint8_t r;
        uint8_t g;
        uint8_t b;
    } ColorRGB;

    // there were plans for supporting FastLED's HSV but... to much effort
    typedef struct PACK {
        union {
            ColorRGB rgb;
        };
    } Color;

    typedef struct PACK {
        uint16_t panelIndex;
        uint8_t state;
        ColorRGB color;
        uint8_t brightness;
    } PanelState;

// BEGIN Common packet structures
    typedef struct PACK {
        packetType_t type;
        uint16_t protocolVersion;
    } PacketHeader;

    typedef struct PACK {
        PacketHeader header;
        uint16_t headerCrc;
    } PacketMeta;
// END

// BEGIN Packets definitions
    typedef struct PACK {
        PacketMeta meta;
        uint16_t panelIndex;
    } PacketInitializationPull;

    typedef struct PACK {
        PacketMeta meta;
        uint16_t panelIndex;
        uint16_t edgeIndex;
    } PacketRegisterEdge;

    typedef struct PACK {
        PacketMeta meta;
        uint8_t on;
    } PacketTurnOnOff;

    typedef struct PACK {
        PacketMeta meta;
        Color color;
    } PacketSetColor;

    typedef struct PACK {
        PacketMeta meta;
        uint8_t brightness;
    } PacketSetBrightness;

    typedef struct PACK {
        PacketMeta meta;
        Color color;
        uint8_t brightness;
    } PacketSetColorAndBrightness;

    typedef struct PACK {
        PacketMeta meta;
        uint16_t panelIndex;
        uint8_t edgeIndex;
        uint16_t connectedPanelIndex;
    } PacketPanelEdgeInfo;

    typedef struct PACK {
        PacketMeta meta;
        PanelState panelState;
    } PacketPanelState;

    typedef struct PACK {
        PacketMeta meta;
        bool useGammaCorrection;
        ColorTemperature colorTemperature;
        LEDColorCorrection colorCorrection;
    } PacketPanelConfiguration;
// END

    uint8_t validatePacket(void *packet, uint8_t size);
    void setPacketMeta(void *packet, packetType_t type);

    const uint8_t MIN_PACKET_SIZE = sizeof(PacketMeta);
}
