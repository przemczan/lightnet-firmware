#pragma once

#define LED_DATA_PIN 3

#include <Arduino.h>
#include "../Common/Protocol.hpp"
#include "../Utils/Macros.hpp"
#include "FastLED.h"

#ifdef ARDUINO_ARCH_ESP32
    #include "analogWrite.h"
#endif

class RGBController
{
    private:
        Protocol::ColorRGB colorValue = { .r = 0xFF, .g = 0xFF, .b = 0xFF };
        uint8_t brightnessValue = 0xFF;
        bool isOn = false;
        CRGB leds[1];

        void updateOutputs();

    public:
        RGBController();
        void turnOn();
        void turnOff();
        bool on();
        Protocol::ColorRGB color();
        void color(uint8_t r, uint8_t g, uint8_t b);
        void color(Protocol::ColorRGB *color);
        uint8_t brightness();
        void brightness(uint8_t brightness);
};
