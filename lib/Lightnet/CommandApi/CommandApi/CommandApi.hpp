#pragma once

#include <Arduino.h>

#define PACK __attribute__((__packed__))

namespace CommandApi
{
    enum command_t: uint8_t {
        CMD_TOGGLE = 1,
        CMD_SET_BRIGHTNESS = 2,
        CMD_SET_COLOR = 3
    };

    typedef struct PACK {
        uint8_t r;
        uint8_t g;
        uint8_t b;
    } ColorRGB;

    typedef struct PACK {
        command_t type;
        uint16_t protocolVersion;
    } CommandHeader;

    typedef struct PACK {
        CommandHeader header;
        uint16_t headerCrc;
        uint16_t dataCrc;
    } Command;

    typedef struct PACK {
        Command meta;
        uint8_t address;
        bool state;
    } CommandToggle;

    typedef struct PACK {
        Command meta;
        uint8_t address;
        uint8_t brightness;
    } CommandSetBrightness;

    typedef struct PACK {
        Command meta;
        uint8_t address;
        ColorRGB color;
    } CommandSetColor;
}
