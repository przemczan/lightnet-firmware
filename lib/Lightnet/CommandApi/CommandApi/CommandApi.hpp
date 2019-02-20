#pragma once

#include <Arduino.h>
#include "Protocol.hpp"
#include "Crc.hpp"

#define PACK __attribute__((__packed__))

namespace CommandApi
{
    const uint16_t VERSION = 0x01;

    typedef struct PACK {
        uint8_t r;
        uint8_t g;
        uint8_t b;
    } ColorRGB;

    // ========= Commands

    namespace Cmd {
        enum command_t: uint8_t {
            TOGGLE = 1,
            SET_BRIGHTNESS = 2,
            SET_COLOR = 3,
            GET_PANELS_LIST = 100,
            GET_PANELS_STATES = 101
        };

        typedef struct PACK {
            command_t type;
            uint16_t protocolVersion;
            uint32_t nonce;
        } CommandHeader;

        typedef struct PACK {
            CommandHeader header;
            uint16_t headerCrc;
            uint16_t payloadCrc;
            uint16_t payloadSize;
            uint8_t payload[];
        } CommandMeta;

        typedef struct PACK {
            CommandMeta meta;
            uint8_t address;
            bool state;
        } Toggle;

        typedef struct PACK {
            CommandMeta meta;
            uint8_t address;
            uint8_t brightness;
        } SetBrightness;

        typedef struct PACK {
            CommandMeta meta;
            uint8_t address;
            ColorRGB color;
        } SetColor;

        typedef struct PACK {
            CommandMeta meta;
            uint16_t length;
            Protocol::PanelState states[];
        } GetPanelsStatesResponse;

        typedef struct PACK {
            CommandMeta meta;
            uint16_t length;
        } GetPanelsListResponse;

        void updateMeta(CommandMeta *meta, command_t type, uint16_t payloadSize);
    }

    // ========== Internal Messages

    namespace Msg {
        typedef struct PACK {
            uint32_t clientId;
            uint16_t payloadSize;
            uint8_t payload[];
        } Message;

        typedef struct PACK {
            Message meta;
            Cmd::GetPanelsStatesResponse panels;
        } PanelsStates;
    }
}
