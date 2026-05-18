#pragma once

#include <Arduino.h>
#include "../Utils/Crc.hpp"
#include <FastLED.h>

#define PACK __attribute__((__packed__))

namespace Protocol {

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
        PACKET_ANIMATION_PREPARE = 12,       // new: animation framework
        PACKET_ANIMATION_START = 13,         // new: animation framework (General Call)
        PACKET_ANIMATION_CONTROL = 14,       // new: animation framework
        PACKET_FETCH_ANIM_STATE = 15,        // new: animation framework
        PACKET_ANIMATION_UPDATE_PARAMS = 16, // new: animation framework (General Call)
        PACKET_RESET_DEVICE = 200,
        PACKET_ENTER_BOOTLOADER = 201,
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

    // Animation Framework Packets (see AnimationTypes.hpp for details)
    // Forward declarations - full defs in AnimationTypes.hpp
    typedef struct PACK {
        PacketMeta meta;
        uint8_t    animType;
        uint8_t    group_id;
        uint8_t    flags;
        uint8_t    transitionMs;
        uint16_t   durationMs;
        ColorRGB   colorFrom;
        ColorRGB   colorTo;
        uint8_t    brightnessFrom;
        uint8_t    brightnessTo;
        uint8_t    param1;
        uint8_t    param2;
    } PacketAnimationPrepare;  // 21 bytes

    typedef struct PACK {
        PacketMeta meta;
        uint8_t    seq_id;
        uint8_t    group_id;
    } PacketAnimationStart;  // 7 bytes

    typedef struct PACK {
        PacketMeta meta;
        uint8_t    cmd;
    } PacketAnimationControl;  // 6 bytes

    typedef struct PACK {
        PacketMeta meta;
        uint8_t    seq_id;
        uint8_t    group_id;
        uint8_t    param_type;
        uint8_t    value;
        uint8_t    transitionMs;
    } PacketAnimationUpdateParams;  // 10 bytes

    typedef struct PACK {
        PacketMeta meta;
        uint8_t    animType;
        uint8_t    group_id;
        uint16_t   elapsedMs;
        uint16_t   durationMs;
        uint8_t    queueLen;
    } PacketAnimationStatus;  // 11 bytes
// END

    // Both controller and panel must agree on this value.
    // Panel side: checked in handleEnterBootloader().
    // Controller side: written into PacketEnterBootloader.token.
    const uint8_t BOOTLOADER_ENTRY_TOKEN = 0xB0;

    // token must equal BOOTLOADER_ENTRY_TOKEN to prevent accidental triggering
    typedef struct PACK {
        PacketMeta meta;
        uint8_t    token;
    } PacketEnterBootloader;  // 6 bytes

    uint8_t validatePacket(void *packet, uint8_t size);
    void setPacketMeta(void *packet, packetType_t type);

    const uint8_t MIN_PACKET_SIZE = sizeof(PacketMeta);

    namespace Colors {
        const Color RED = { { 255, 0, 0 } };
        const Color GREEN = { { 0, 255, 0 } };
        const Color BLUE = { { 0, 0, 255 } };
        const Color WHITE = { { 255, 255, 255 } };
    }
}
