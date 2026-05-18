#pragma once

#include <Arduino.h>
#include "../Common/Protocol.hpp"
#include "../Utils/Crc.hpp"

#define PACK __attribute__((__packed__))

namespace MessageApi
{
    const uint16_t VERSION = 0x01;

    typedef struct PACK {
        uint8_t r;
        uint8_t g;
        uint8_t b;
    } ColorRGB;

    enum packet_t: uint8_t {
        TOGGLE = 1,
        SET_BRIGHTNESS = 2,
        SET_COLOR = 3,
        GET_EDGES_LIST = 4,
        GET_PANELS_STATES = 5,
        PANELS_STATES = 6,
        EDGES_LIST = 7,
    };

    typedef struct PACK {
        packet_t type;
        uint16_t protocolVersion;
        uint32_t nonce;
    } PacketHeader;

    typedef struct PACK {
        PacketHeader header;
        uint16_t headerCrc;
        uint16_t payloadCrc;
        uint16_t payloadSize;
        uint8_t payload[0];
    } PacketMeta;

    void updatePacketMeta(PacketMeta *meta, packet_t type, uint16_t payloadSize);

    typedef struct PACK {
        uint16_t panelIndex;
        uint16_t edgeIndex;
        uint16_t connectedPanelIndex;
        uint16_t connectedEdgeIndex;
    } PanelEdgeInfo;

    typedef Protocol::PanelState PanelState;

    namespace Cmd {

        typedef struct PACK {
            PacketMeta meta;
            uint8_t address;
            bool state;
        } Toggle;

        typedef struct PACK {
            PacketMeta meta;
            uint8_t address;
            uint8_t brightness;
        } SetBrightness;

        typedef struct PACK {
            PacketMeta meta;
            uint8_t address;
            ColorRGB color;
        } SetColor;
    }

    namespace Rsp {
        typedef struct PACK {
            PacketMeta meta;
            uint16_t length;
            PanelState states[];
        } PanelsStates;

        typedef struct PACK {
            PacketMeta meta;
            uint16_t length;
            PanelEdgeInfo edges[];
        } EdgesList;
    }

    namespace Internal {
        typedef struct PACK {
            uint32_t clientId;
            uint16_t payloadSize;
            uint8_t payload[0];
        } Message;

        typedef struct PACK {
            Message meta;
            Rsp::PanelsStates panelsStates;
        } PanelsStates;

        typedef struct PACK {
            Message meta;
            Rsp::EdgesList edgesList;
        } EdgesList;
    }
}
