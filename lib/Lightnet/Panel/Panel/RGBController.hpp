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

        Protocol::ColorRGB colorValue = { .r = 0xFF, .g = 0xFF, .b = 0xFF };
        uint8_t brightnessValue = 0;
        bool isOn = false;

    private:
        void updateOutputs();

    public:
        RGBController(uint8_t _rPinNo, uint8_t _gPinNo, uint8_t _bPinNo);
        void turnOn();
        void turnOff();
        bool on();
        Protocol::ColorRGB color();
        void color(uint8_t r, uint8_t g, uint8_t b);
        void color(Protocol::ColorRGB *color);
        uint8_t brightness();
        void brightness(uint8_t brightness);
};
