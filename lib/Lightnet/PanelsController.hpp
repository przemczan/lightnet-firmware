#pragma once

#include <Arduino.h>
#include "LightnetBus.hpp"
#include "Protocol.hpp"

class PanelsController
{
    public:
        uint8_t setColorAndBrightness(uint8_t address, Protocol::Color *color, uint8_t brightness);
        uint8_t setColor(uint8_t address, Protocol::Color *color);
        uint8_t setBrightness(uint8_t address, uint8_t brightness);
        uint8_t turnOnOff(uint8_t address, uint8_t on);
        uint8_t turnOn(uint8_t address);
        uint8_t turnOff(uint8_t address);
        void resetDevices(uint16_t maxIndex);
};
