#pragma once

#include <Arduino.h>
#include "Protocol.hpp"
#include "Macros.hpp"

#ifdef ARDUINO_ARCH_ESP32
#include "analogWrite.h"
#endif

class RGBController
{
    private:
        uint8_t rPinNo;
        uint8_t gPinNo;
        uint8_t bPinNo;

        Protocol::ColorRGB values = { .r = 0xFF, .g = 0xFF, .b = 0xFF };
        uint8_t brightness = 0;
        bool on = false;

    private:
        void updateOutputs();

    public:
        RGBController(uint8_t _rPinNo, uint8_t _gPinNo, uint8_t _bPinNo);
        void turnOn();
        void turnOff();
        void setColor(uint8_t r, uint8_t g, uint8_t b);
        void setColor(Protocol::ColorRGB *color);
        void setBrightness(uint8_t brightness);
};
