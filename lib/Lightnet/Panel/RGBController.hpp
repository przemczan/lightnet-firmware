#pragma once

#include <Arduino.h>
#include "Protocol.hpp"
#include "Macros.hpp"

class RGBController
{
    private:
        uint8_t rPinNo;
        uint8_t gPinNo;
        uint8_t bPinNo;

        Protocol::ColorRGB values;

    private:
        void updateOutputs();

    public:
        RGBController(uint8_t _rPinNo, uint8_t _gPinNo, uint8_t _bPinNo);
        void turnOn();
        void turnOff();
        void setValues(uint8_t r, uint8_t g, uint8_t b);
        void setColor(Protocol::ColorRGB *color);
};
