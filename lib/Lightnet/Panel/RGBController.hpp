#pragma once

#define LED_DATA_PIN PD5

#include <Arduino.h>
#include "../Common/Protocol.hpp"
#include "../Utils/Macros.hpp"
#include "FastLED.h"
#include "../Utils/Gamma.hpp"

#ifdef ARDUINO_ARCH_ESP32
    #include "analogWrite.h"
#endif

class RGBController
{
    private:
        Protocol::ColorRGB colorValue = { .r = 0xFF, .g = 0xFF, .b = 0xFF };
        uint8_t brightnessValue = 0xFF;
        uint8_t globalBrightnessValue = 0xFF;  // applied to every output frame, 0..255
        bool isOn = false;
        CRGB leds[1];
        bool useGammaCorrection = true;
        LEDColorCorrection colorCorrection = UncorrectedColor;
        ColorTemperature colorTemperature = UncorrectedTemperature;

        // Tracking fields for delta-based debug logging (DEBUG_RGB_CTRL)
        Protocol::ColorRGB lastLogColor = { 0, 0, 0 };
        uint8_t lastLogBrightness = 0;
        uint8_t lastLogGlobal = 0;
        bool lastLogOn = false;

        void updateOutputs();
        void maybeLog();

    public:
        RGBController();
        void turnOn();
        void turnOff();
        void gammaCorrection(bool use);
        void setColorCorrection(LEDColorCorrection colorCorrection);
        void setColorTemperature(ColorTemperature colorTemperature);
        bool on();
        Protocol::ColorRGB color();
        void color(uint8_t r, uint8_t g, uint8_t b);
        void color(Protocol::ColorRGB *color);
        uint8_t brightness();
        void brightness(uint8_t brightness);
        void globalBrightness(uint8_t value);  // 0..255 multiplier on the final output
        uint8_t globalBrightness() const
        {
            return globalBrightnessValue;
        }
};
