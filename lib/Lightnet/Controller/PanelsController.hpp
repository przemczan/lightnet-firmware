#pragma once

#include <Arduino.h>
#include "../Common/LightnetBus.hpp"
#include "../Common/Protocol.hpp"

class PanelsController
{
    typedef struct {
        bool useGammaCorrection;
    } panelConfiguration_t;

    public:
        uint8_t setColorAndBrightness(uint8_t address, Protocol::Color *color, uint8_t brightness);
        uint8_t setColor(uint8_t address, Protocol::Color *color);
        uint8_t setBrightness(uint8_t address, uint8_t brightness);
        uint8_t turnOnOff(uint8_t address, uint8_t on);
        uint8_t turnOn(uint8_t address);
        uint8_t turnOff(uint8_t address);
        uint8_t fetchState(uint8_t address, Protocol::PanelState *state);
        uint8_t sendConfiguration(uint8_t address, panelConfiguration_t);
        void resetDevices(uint16_t maxIndex);
};
